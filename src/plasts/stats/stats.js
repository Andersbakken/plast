/*global paper, console, JSON, WebSocket, window, localStorage, Josh*/

var ws, scheduler;
var mode, mainmode, common, logmode, config;
var unusedColor = "purple";
var options = Object.create(null);

function getParameterByName(name)
{
    name = name.replace(/[\[]/, "\\[").replace(/[\]]/, "\\]");
    var regex = new RegExp("[\\?&]" + name + "=([^&#]*)"),
        results = regex.exec(window.location.search);
    return results === null ? "" : decodeURIComponent(results[1].replace(/\+/g, " "));
}

var colorOverrides = (function() {
    var ret = Object.create(null);
    var overrides = getParameterByName("colors").split(',');
    for (var i=0; i<overrides.length; ++i) {
        var split = overrides[i].split(':');
        if (split.length == 2) {
            ret[split[0]] = split[1];
        }
    }
    return ret;
})();

function generateColor(peer)
{
    function rainbow(numOfSteps, step) {

        // This function generates vibrant, "evenly spaced" colours (i.e. no clustering). This is ideal for creating easily distinguishable vibrant markers in Google Maps and other apps.
        // Adam Cole, 2011-Sept-14
        // HSV to RBG adapted from: http://mjijackson.com/2008/02/rgb-to-hsl-and-rgb-to-hsv-color-model-conversion-algorithms-in-javascript
        var r, g, b;
        var h = step / numOfSteps;
        var i = ~~(h * 6);
        var f = h * 6 - i;
        var q = 1 - f;
        switch (i % 6 ){
        case 0: r = 1, g = f, b = 0; break;
        case 1: r = q, g = 1, b = 0; break;
        case 2: r = 0, g = 1, b = f; break;
        case 3: r = 0, g = q, b = 1; break;
        case 4: r = f, g = 0, b = 1; break;
        case 5: r = 1, g = 0, b = q; break;
        }
        var c = "#" + ("00" + (~ ~(r * 255)).toString(16)).slice(-2) + ("00" + (~ ~(g * 255)).toString(16)).slice(-2) + ("00" + (~ ~(b * 255)).toString(16)).slice(-2);
        return (c);
    }

    if (peer in colorOverrides) {
        return colorOverrides[peer];
    }
    var hash = 0;
    // var codes = [];
    function hashCharCode(idx) {
        // console.log("hashing " + idx + " " + peer.charAt(idx) + " " + peer.charCodeAt(idx));
        hash = ((hash << 5) - hash) + peer.charCodeAt(idx);
    }
    var count = Math.floor(peer.length / 2);
    for (var i=0; i<count; ++i) {
        hashCharCode(i);
        hashCharCode(peer.length - i - 1);
    }
    if (peer % 2 == 1) {
        hashCharCode(count);
    }
    hash = Math.abs(hash);
    var mod = 1024 * 1024 * 16;
    return rainbow(mod, hash % mod);
}

function complementaryColor(color) {
    var col = color.match(/#([0-9][0-9][0-9][0-9][0-9][0-9])/);
    if (col && col.length == 2) {
        return ('000000' + (('0xffffff' ^ col).toString(16))).slice(-6);
    }
    switch (color) {
    case 'red': return 'black';
    case 'white': return 'blue';
    case 'black': return 'yellow';
    case 'green': return 'white';
    case 'blue': return 'white';
    case 'magenta': return 'black';
    case 'yellow': return 'blue';
    }
    return "0xFFFFFF";
}

function peerClicked()
{
    window.location.hash = '#detail-' + this.peerName;
}

function Config(ws)
{
    this._ws = ws;
    this._input = document.querySelector('#console > input');
    this._ctx = document.querySelector('#console > div.inputctx');
    this._readline = new Josh.ReadLine();
    this._readline.onChange(function(line) {
        config._line = line;
        config._render();
    });
    this._readline.onCompletion(function(line, callback) {
        if(!line || !line.text) {
            return callback();
        }
        if (line.text[0] === '/') {
            // before and after cursor;
            var before = line.text.substring(1, line.cursor);
            var after = line.text.substr(line.cursor);
            // find suggestions that starts with before and ends with after
            var alts = [], maxlen = 0, max;
            for (var k in config._handlers) {
                if (k.substr(0, before.length) === before) {
                    if (k.substr(k.length - after.length, after.length) === after) {
                        alts.push(k);
                        if (k.length > maxlen) {
                            max = k;
                            maxlen = k.length;
                        }
                    }
                }
            }
            if (alts.length === 1) {
                if (alts[0] === line.text.substr(1)) {
                    return callback(' ');
                } else {
                    return callback(alts[0].substr(line.text.length - 1) + ' ');
                }
            } else if (alts.length) {
                // find common stem
                var sz = alts.length, i;
                var done = false, test;
                while (!done) {
                    test = max.substr(0, maxlen);
                    if (!maxlen)
                        break;
                    done = true;
                    for (i = 0; i < sz; ++i) {
                        if (alts[i].substr(0, maxlen) !== test) {
                            --maxlen;
                            done = false;
                            break;
                        }
                    }
                }
                for (i = 0; i < sz; ++i) {
                    config.log('/' + alts[i]);
                }
                return callback(test.substr(line.text.length - 1));
            }
            return callback();
        } else {
            return callback();
        }
    });
    this._readline.onEnter(function(cmdtext, callback) {
        // console.log("enter", cmdtext);
        config._line = undefined;
        config._render();
        config.run(cmdtext);
        callback("");
    });
    this._readline.onClear(function() {
        // console.log("clear");
        config._input.value = "";
    });
    this._readline.onSearchStart(function() {
        config._prefix = "bck-i-search: ";
        // console.log("search start");
    });
    this._readline.onSearchEnd(function() {
        config._search = undefined;
        config._prefix = "";
        // console.log("search end");
    });
    this._readline.onSearchChange(function(search) {
        config._search = search;
        config._render();
        // console.log("search change", search);
    });
};

Config.prototype = {
    constructor: Config,
    _readline: undefined,
    _ws: undefined,
    _input: undefined,
    _ctx: undefined,
    _prefix: "",
    _search: undefined,
    _line: undefined,
    _handlers: {
        help: function() {
            console.log('help here');
        },
        peers: function() {
            config._sendCommand('peers');
        },
        block: function(args) {
            config._sendCommand('block', args);
        },
        unblock: function(args) {
            config._sendCommand('unblock', args);
        }
    },
    _render: function() {
        var c;
        if (this._search) {
            if (this._search.text)
                this._input.value = this._search.text;
            // set the cursor position
            c = this._search.cursoridx || 0;
            this._input.selectionStart = c;
            this._input.selectionEnd = c;
            this._ctx.innerHTML = this._prefix + (this._search.term || "");
        } else if (this._line) {
            this._input.value = this._line.text;
            // set the cursor position
            c = this._line.cursor || 0;
            this._input.selectionStart = this._prefix.length + c;
            this._input.selectionEnd = this._prefix.length + c;
            this._ctx.innerHTML = "";
        } else {
            this._input.value = "";
            this._ctx.innerHTML = "";
        }
    },
    _sendCommand: function(cmd, args) {
        this._ws.send(JSON.stringify({ cmd: cmd, args: args }));
    },
    handleCommand: function(cmd) {
        var args = cmd.split(' ');
        var h = this._handlers[args.shift()];
        if (typeof h === 'function') {
            h(args);
        }
    },
    toggle: function() {
        var cfg = document.getElementById('config');
        if (cfg.getAttribute('class') === 'active') {
            cfg.setAttribute('class', 'inactive');
            this._readline.deactivate();
        } else {
            cfg.setAttribute('class', 'active');
            window.setTimeout(function() {
                document.querySelector('#console > input').focus();
            }, 100);
            this._readline.activate();
        }
    },
    log: function(txt) {
        var p = document.createElement('p');
        p.appendChild(document.createTextNode(txt));
        var content = document.querySelector('#console > div.content');
        var bottom = content.scrollTop + content.clientHeight >= content.scrollHeight;
        content.appendChild(p);
        if (bottom)
            content.scrollTop = content.scrollHeight;
    },
    run: function(line) {
        if (line.length > 0 && line[0] === '/') {
            this.handleCommand(line.substr(1));
        } else {
            this.log(line);
        }
    }
};

function Common(canvas)
{
    this.canvas = canvas;
    this.init();
};

Common.prototype = {
    canvas: undefined,
    _pixelRatio: undefined,
    init: function() {
        var ctx = this.canvas.getContext("2d");
        this._pixelRatio = ctx.webkitBackingStorePixelRatio
            || ctx.mozBackingStorePixelRatio
            || ctx.msBackingStorePixelRatio
            || ctx.oBackingStorePixelRatio
            || ctx.backingStorePixelRatio
            || 1;
    },
    center: function(rel) {
        var pt = new paper.Point(this.width() / 2, this.height() / 2);
        if (rel) {
            pt.x -= rel.width / 2;
            pt.y -= rel.height / 2;
        }
        return pt;
    },
    radians: function(pt) {
        var cx = this.width() / 2;
        var cy = this.height() / 2;
        var r = Math.atan2(pt.y - cy, pt.x - cx);
        if (r < 0)
            r += Math.PI * 2;
        return r;
    },
    width: function() {
        return this.canvas.width / ((window.devicePixelRatio || 1) / this._pixelRatio);
    },
    height: function() {
        return this.canvas.height / ((window.devicePixelRatio || 1) / this._pixelRatio);
    }
};

function LogMode()
{
    this._log = document.getElementById('log');
}

LogMode.prototype = {
    constructor: LogMode,
    _log: undefined,
    _count: 0,
    _maxCount: 1024,
    _active: Object.create(null),
    processMessage: function(msg) {
        if (msg.type !== "build")
            return;

        var span;
        function updateTooltip()
        {
            var str = ("Local: " + msg.local +
                       "\nFile: " + msg.file +
                       "\nPeer: " + msg.peer +
                       "\nJobId: " + msg.jobid +
                       "\nStarted: " + span.started.toString());
            if (span.finished) {
                str += ("\nFinished: " + span.finished.toString() +
                        "\nTime: " + (span.finished.valueOf() - span.started.valueOf()) + "ms");
            }
            // "\nArgs: " + (msg.args ? msg.args : ""));
            span.title = str;
        }

        function updateSpan() {
            span.started = new Date();
            function fileName() {
                var lastSlash = msg.file.lastIndexOf('/');
                return lastSlash === -1 ? msg.file : msg.file.substr(lastSlash + 1);
            }

            updateTooltip();

            // span.style.backgroundColor = generateColor(msg.local); // ### this is a little ugly
            span.firstChild.textContent = msg.local + " builds " + fileName() + (msg.local !== msg.peer ? (" for " + msg.peer) : "");
        }
        if (msg.start) {
            span = this._active[msg.jobid];
            if (!span) {
                while (this._count >= this._maxCount) {
                    --this._count;
                    delete this._active[this._log.firstChild.jobid];
                    this._log.removeChild(this._log.firstChild);
                    this._log.removeChild(this._log.firstChild);
                }

                ++this._count;
                var bottom = this._log.scrollTop + this._log.clientHeight >= this._log.scrollHeight;
                // console.log(this._log.scrollTop + " " + this._log.scrollHeight);
                span = document.createElement("span");
                span.jobid = msg.jobid;
                span.setAttribute("class", "active");
                span.appendChild(document.createTextNode(""));
                updateSpan();
                this._log.appendChild(span);
                this._log.appendChild(document.createElement("br"));
                span.msg = msg;
                this._active[msg.jobid] = span;
                if (bottom)
                    this._log.scrollTop = this._log.scrollHeight;
            } else {
                span.msg = msg;
                updateSpan();
            }
        } else {
            span = this._active[msg.jobid];
            if (!span || span.msg.local != msg.local)
                return;

            span.firstChild.textContent = span.firstChild.textContent.replace(" builds ", " built ");
            span.finished = new Date();
            updateTooltip();
            span.setAttribute("class", "inactive");
            delete this._active[msg.jobid];
        }
    }
};

function Pie(args)
{
    this._radius = Math.min(common.width(), common.height()) / 3;
    this._circle = new paper.Path.Circle(common.center(), this._radius);
    this._circle.fillColor = unusedColor;
    this._legends = new paper.Group({ position: new paper.Point(20, 20) });

    this._addConfig();

    var pie = this;
    paper.view.onFrame = function(e) { pie._onframe.call(pie, e); };
}

Pie.prototype = {
    _peers: Object.create(null),
    _totalJobs: 0,
    _running: Object.create(null),
    _radius: undefined,
    _circle: undefined,
    _legends: undefined,

    constructor: Pie,
    processMessage: function(msg) {
        if (msg.type === "peer") {
            if (msg.delete) {
                if (msg.id in this._peers) {
                    var p = this._peers[msg.id];
                    p.legend.remove();
                    this._totalJobs -= p.msg.jobs;
                    delete this._peers[msg.id];
                }
            } else {
                var group = new paper.Group();
                this._peers[msg.id] = { msg: msg, legend: group };
                this._totalJobs += msg.jobs;
                this._addLegend(msg.name, group);
            }
            this._recalc();
        } else if (msg.type === "build") {
            if (msg.start)
                this._addRunning(msg.peer);
            else
                this._removeRunning(msg.peer);
            this._recalc();
        } else {
            config.log(JSON.stringify(msg));
        }
    },

    _addLegend: function(peer, group) {
        var color = generateColor(peer);
        var c = common.center();
        var txt = new paper.PointText({point: new paper.Point(20, 10),
                                       justification: 'left',
                                       fontSize: 16,
                                       fillColor: color});
        txt.content = peer;
        group.onClick = function() { peerClicked(txt.content); };
        group.addChild(txt);
        this._legends.addChild(group);
        this._rearrangeLegends();
    },
    _addConfig: function() {
        var cfg = new paper.PointText({point: new paper.Point(common.width() - 50, common.height() - 10),
                                       justification: 'left',
                                       fontSize: 45,
                                       fillColor: '#555',
                                       content: '\u2699'});
        var that = this;
        cfg.onClick = function() { config.toggle(); };
        paper.project.activeLayer.addChild(cfg);
        paper.view.draw();
    },
    _rearrangeLegends: function() {
        var cnt = 0;
        var sorted = this._legends.children.sort(function(a, b) { return a.children[0].content > b.children[0].content; });
        for (var i=0; i<sorted.length; ++i) {
            var p = sorted[i];
            p.position.y = (24 * cnt++) + 10;
        }
        paper.project.activeLayer.addChild(this._legends);
        paper.view.draw();
    },
    _addRunning: function(peer) {
        if (peer in this._running) {
            this._running[peer].count += 1;
        } else {
            var path = new paper.Path(common.center());
            path.fillColor = generateColor(peer);
            var text = new paper.PointText();
            text.content = peer;
            text.fillColor = "black";
            this._running[peer] = {
                count: 1,
                diff: { start: 0, rad: 0 },
                end: { start: 0, rad: 0 },
                path: path,
                text: text
            };
        }
    },
    _removeRunning: function(peer) {
        if (peer in this._running) {
            if (this._running[peer].count === 1) {
                this._deinit(this._running[peer]);
                delete this._running[peer];
            } else {
                --this._running[peer].count;
            }
        }
    },
    _deinit: function(peer) {
        if (peer.path)
            peer.path.remove();
        if (peer.text)
            peer.text.remove();
    },
    _onframe: function(e) {
        // animate the segments
        var c = common.center();
        for (var name in this._running) {
            var peer = this._running[name];
            // console.log(name, peer);
            if (peer.diff.start || peer.diff.rad) {
                var startrad = common.radians(peer.path.segments[1].point);
                var endrad = common.radians(peer.path.segments[peer.path.segments.length - 1].point);
                // console.log("moving " + name + " " + startrad + " closer to " + peer.end.start + " by " + peer.diff.start + "(" + (startrad + peer.diff.start) + ")");
                // console.log("  "  + endrad + " closer to " + (peer.end.start + peer.end.rad) + " by " + peer.diff.rad + " (" + (endrad + peer.diff.rad) + ")");
                // console.log(endrad, peer.diff.rad);
                startrad += peer.diff.start;
                endrad += peer.diff.rad;
                if (startrad > endrad) {
                    var tmp = startrad;
                    startrad = endrad;
                    endrad = tmp;
                }
                if (peer.diff.start >= 0 && startrad >= peer.end.start) {
                    startrad = peer.end.start;
                    peer.diff.start = 0;
                } else if (peer.diff.start < 0 && startrad <= peer.end.start) {
                    startrad = peer.end.start;
                    peer.diff.start = 0;
                }
                if (peer.diff.rad >= 0 && endrad >= peer.end.start + peer.end.rad) {
                    endrad = peer.end.start + peer.end.rad;
                    peer.diff.rad = 0;
                } else if (peer.diff.rad < 0 && endrad <= peer.end.start + peer.end.rad) {
                    endrad = peer.end.start + peer.end.rad;
                    peer.diff.rad = 0;
                }
                var throughrad = startrad + ((endrad - startrad) / 2);
                peer.path.removeSegments(1);
                peer.path.add(new paper.Point(c.x + (this._radius * Math.cos(startrad)),
                                              c.y + (this._radius * Math.sin(startrad))));
                var thpt = new paper.Point(c.x + (this._radius * Math.cos(throughrad)),
                                           c.y + (this._radius * Math.sin(throughrad)));
                var ept = new paper.Point(c.x + (this._radius * Math.cos(endrad)),
                                          c.y + (this._radius * Math.sin(endrad)));
                peer.path.arcTo(thpt, ept);
                peer.path.closePath();
                this._updateText(peer.text, c, throughrad);
                // console.log("  ended up at " + common.radians(peer.path.segments[peer.path.segments.length - 1].point), thpt, ept, peer.path.segments[peer.path.segments.length - 1].point, peer.path.segments[1].point, throughrad);
            }
        }
        // console.log("--");
    },
    _updateText: function(text, center, through) {
        var labelTurn = new paper.Point(1.25 * this._radius * Math.cos(through) + center.x,
                                        1.25 * this._radius * Math.sin(through) + center.y);
        if (labelTurn.x >= center.x) { // turn right
            text.position = new paper.Point(labelTurn.x + 15, labelTurn.y);
        } else {
            text.position = new paper.Point(labelTurn.x - 15, labelTurn.y);
        }
    },
    _recalc: function(force) {
        // make a pie
        if (force) {
            paper.project.activeLayer.removeChildren();
            this._radius = Math.min(common.width(), common.height()) / 3;
            this._circle = new paper.Path.Circle(common.center(), this._radius);
            this._circle.fillColor = unusedColor;
            this._addConfig();
            this._rearrangeLegends();
        }
        if (Object.keys(this._running).length === 0) {
            if (force) {
                paper.project.activeLayer.addChild(this._circle);
                return;
            }
            // noone is building, make a circle
            paper.project.activeLayer.removeChildren();
            paper.project.activeLayer.addChild(this._circle);
            this._addConfig();
            this._rearrangeLegends();
        } else {
            paper.project.activeLayer.addChild(this._circle);
            // make and/or animate arcs
            var time = (1 / 30); // 500ms
            var c = common.center(), cur = 0, arc, where = 0, to = 0;
            var pt = new paper.Point(c.x + this._radius, c.y);
            var diff = (Math.PI * 2) / this._totalJobs, end;
            var moved = 1;
            for (var name in this._running) {
                var peer = this._running[name];
                arc = peer.count * diff;
                if (peer.path.segments.length < 3) {
                    // not calculated yet, make new
                    end = new paper.Point(c.x + (this._radius * Math.cos(cur + arc)),
                                          c.y + (this._radius * Math.sin(cur + arc)));
                    peer.path.add(pt);
                    var through = new paper.Point(c.x + (this._radius * Math.cos(cur + (arc / 2))),
                                                  c.y + (this._radius * Math.sin(cur + (arc / 2))));
                    // console.log("new peer " + name, pt, end, through);
                    peer.path.arcTo(through, end);
                    peer.path.closePath();
                    this._updateText(peer.text, c, cur + (arc / 2));
                } else {
                    // calculate diff and end anims
                    where = common.radians(peer.path.segments[1].point) - where;
                    var endref = peer.path.segments[peer.path.segments.length - 1].point;
                    end = new paper.Point(endref.x, endref.y);
                    to = common.radians(end) - where;
                    peer.end.start = cur;
                    peer.end.rad = arc;
                    peer.diff.start = (cur - where) * time * moved;
                    peer.diff.rad = (arc - to) * time;
                    if (peer.diff.start)
                        moved += 1;
                }
                paper.project.activeLayer.addChild(peer.path);
                cur += arc;
                pt = end;
            }
        }
        paper.view.draw();
    }
};

function Detail(args)
{
    this.type = "detail";
    this.local = args.name;
    this.dom = document.getElementById('detail');
    var header = document.getElementById('header');
    while (header.firstChild) {
        header.removeChild(header.firstChild);
    }
    var back = document.createElement('a');
    back.setAttribute('href', '#');
    back.setAttribute('class', 'back');
    var backtxt = document.createTextNode('\u2B05');
    back.appendChild(backtxt);
    header.appendChild(back);
    var h1 = document.createElement('h1');
    var name = document.createTextNode(this.local);
    h1.appendChild(name);
    header.appendChild(h1);
    var ul = document.createElement('ul');
    var tabs = [{name: 'info', link: '#detail-' + this.local, path: '/info'},
                {name: 'building', link: '#detail-' + this.local, path: '/building'},
                {name: 'builds', link: '#detail-' + this.local, path: '/builds'}];
    for (var t in tabs) {
        var li = document.createElement('li');
        li.setAttribute('path', tabs[t].path);
        var a = document.createElement('a');
        a.setAttribute('class', 'tab');
        a.setAttribute('href', tabs[t].link + tabs[t].path);
        var txt = document.createTextNode(tabs[t].name);
        a.appendChild(txt);
        li.appendChild(a);
        ul.appendChild(li);
    }
    ul.firstChild.setAttribute('id', 'selected');
    header.appendChild(ul);

    // while (this.dom.firstChild) {
    //     this.dom.removeChild(this.dom.firstChild);
    // }
    // this._table = document.createElement('table');
    // this.dom.appendChild(this._table);
};

Detail.prototype = {
    constructor: Detail,
    type: undefined,
    _remoteTable: undefined,
    _localTable: undefined,
    _map: Object.create(null),
    processMessage: function(msg) {
        var row;
        if (msg.type !== "build")
            return;
        if (this._remoteTable) {
            if (msg.local !== this.local)
                return;
            if (msg.start) {
                // insert
                row = this._remoteTable.insertRow(-1);
                this._addColumn(row, msg.file);
                this._addColumn(row, msg.peer);
                this._map[msg.jobid] = row;
            } else {
                if (!(msg.jobid in this._map))
                    return;
                this._remoteTable.deleteRow(this._map[msg.jobid].rowIndex);
                delete this._map[msg.jobid];
            }
        } else if (this._localTable) {
            if (msg.peer !== this.local)
                return;
            if (msg.start) {
                // insert
                row = this._localTable.insertRow(-1);
                this._addColumn(row, msg.file);
                this._addColumn(row, msg.local);
                this._map[msg.jobid] = row;
            } else {
                if (!(msg.jobid in this._map))
                    return;
                this._localTable.deleteRow(this._map[msg.jobid].rowIndex);
                delete this._map[msg.jobid];
            }
        }
        //console.log("message for local:", msg);
    },
    path: function(arg) {
        var lis = document.querySelectorAll('#header > ul > li');
        for (var i = 0; i < lis.length; ++i) {
            var li = lis[i];
            if (li.getAttribute('path') === arg) {
                li.setAttribute('id', 'selected');
            } else {
                li.removeAttribute('id');
            }
        }
        this._updatePath(arg);
    },
    _updatePath: function(path) {
        var content = document.getElementById('content');
        while (content.firstChild) {
            content.removeChild(content.firstChild);
        }
        if (this._remoteTable)
            this._remoteTable = undefined;
        if (this._localTable)
            this._localTable = undefined;
        this._map = Object.create(null);
        if (path === '/building') {
            this._remoteTable = document.createElement('table');
            content.appendChild(this._remoteTable);
        } else if (path === '/builds') {
            this._localTable = document.createElement('table');
            content.appendChild(this._localTable);
        }
    },
    _addColumn: function(row, text) {
        var col = row.insertCell(-1);
        var txt = document.createTextNode(text);
        col.appendChild(txt);
    }
};

function disconnected()
{
    var dis = document.getElementById('disconnected');
    dis.style.display = "flex";
}

var callbacks = {
    websocketOpen: function(evt) {
        console.log("ws open");
        ws.send("hello there");
    },
    websocketClose: function(evt) {
        console.log("ws close");
        disconnected();
    },
    websocketMessage: function(evt) {
        if (!mainmode)
            return;
        var obj;
        try {
            obj = JSON.parse(evt.data);
        } catch (e) {
        }
        if (obj) {
            if (mode !== mainmode) {
                mode.processMessage(obj);
            }
            mainmode.processMessage(obj);
            logmode.processMessage(obj);
        }
    },
    websocketError: function(evt) {
        console.log("ws error " + JSON.stringify(evt));
        disconnected();
    }
};

var oldHash = "";
var mapper = Object.create(null);

function peerClicked(peer)
{
    window.location.hash = '#detail-' + peer + '/info';
};

function init() {
    var checks = document.querySelectorAll('#options > input[type=checkbox]');
    for (var i = 0; i < checks.length; ++i) {
        var check = checks[i];
        var name = check.name;
        var checked = (localStorage.getItem("options." + name) === "on");
        check.checked = checked;
        options[name] = checked;
        check.onchange = function() {
            options[this.name] = this.checked;
            localStorage.setItem("options." + this.name, this.checked ? "on" : "off");
        };
    }

    mapper[""] = document.getElementById('canvas');
    mapper["detail"] = document.getElementById('detail');

    var url = "ws://" + window.location.hostname + ":" + window.location.port + "/";
    ws = new WebSocket(url);
    ws.onopen = callbacks.websocketOpen;
    ws.onclose = callbacks.websocketClose;
    ws.onmessage = callbacks.websocketMessage;
    ws.onerror = callbacks.websocketError;

    var canvas = document.getElementById('canvas');
    paper.setup(canvas);
    common = new Common(canvas);
    config = new Config(ws);
    mode = new Pie();
    mainmode = mode;
    logmode = new LogMode();

    // var path = new paper.Path();
    // path.strokeColor = 'black';
    // var start = new paper.Point(100, 100);
    // path.moveTo(start);
    // path.lineTo(start.add([ 200, -50 ]));
    // paper.view.draw();
}

function findObject(hash)
{
    if (!hash.length) {
        return { dom: mapper[""], data: undefined };
    }
    var dash = hash.indexOf('-');
    var prefix = hash.substring(1, dash);
    var name = hash.substr(dash + 1);
    var path = "";
    var slash = name.indexOf('/');
    if (slash !== -1) {
        path = name.substr(slash);
        name = name.substr(0, slash);
    }
    return { dom: mapper[prefix], data: { prefix: prefix, name: name, path: path } };
}

function update(data)
{
    if (!data) {
        mode = mainmode;
    } else if (data.prefix == "detail") {
        if (mode && mode.type === "detail" && mode.local === data.name) {
            mode.path(data.path);
        } else {
            mode = new Detail({ name: data.name });
            if (data.path.length)
                mode.path(data.path);
        }
    }
}

window.addEventListener('hashchange', function() {
    findObject(oldHash).dom.style.display = 'none';
    var data = findObject(window.location.hash);
    data.dom.style.display = 'block';
    update(data.data);
    oldHash = window.location.hash;
});

window.addEventListener('resize', function() {
    if (mainmode)
        mainmode._recalc(true);
});

window.addEventListener('keypress', function(e) {
    if (e.charCode == 0x60) { // U+0060: GRAVE ACCENT
        config.toggle();
        e.preventDefault();
        e.stopPropagation();
        return false;
    }
    return true;
});

window.location.hash = "";
window.addEventListener("load", init, false);
