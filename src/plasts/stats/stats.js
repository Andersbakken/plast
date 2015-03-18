/*global paper, console, JSON, WebSocket*/

var ws, scheduler;
var mode, mainmode, common;
var colors = [ "red", "blue", "yellow", "green", "cyan" ];
var unusedColor = "purple";
var curColor = 0;
var peerColors = Object.create(null);

function generateColor(peer)
{
    if (peer in peerColors)
        return peerColors[peer];
    var c = colors[curColor++ % colors.length];
    peerColors[peer] = c;
    return c;
}

function peerClicked()
{
    window.location.hash = '#detail-' + this.peerName;
}

function Common(stats)
{
    this.stats = stats;
    this.init();
};

Common.prototype = {
    stats: undefined,
    _pixelRatio: undefined,
    init: function() {
        var ctx = this.stats.getContext("2d");
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
        return this.stats.width / ((window.devicePixelRatio || 1) / this._pixelRatio);
    },
    height: function() {
        return this.stats.height / ((window.devicePixelRatio || 1) / this._pixelRatio);
    }
};

function Pie(args)
{
    this._radius = Math.min(common.width(), common.height()) / 3;
    this._circle = new paper.Path.Circle(common.center(), this._radius);
    this._circle.fillColor = unusedColor;
    this._legends = new paper.Group({ position: new paper.Point(20, 20) });

    var pie = this;
    paper.view.onFrame = function(e) { pie._onframe.call(pie, e); };
};

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
            console.log("unhandled message", msg);
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
    _rearrangeLegends: function() {
        var cnt = 0;
        for (var i in this._legends.children) {
            var p = this._legends.children[i];
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
            path.fillColor = peerColors[peer];
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
    _recalc: function() {
        // make a pie
        if (Object.keys(this._running).length === 0) {
            // noone is building, make a circle
            paper.project.activeLayer.removeChildren();
            paper.project.activeLayer.addChild(this._circle);
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

var callbacks = {
    websocketOpen: function(evt) {
        console.log("ws open");
        ws.send("hello there");
    },
    websocketClose: function(evt) {
        console.log("ws close");
    },
    websocketMessage: function(evt) {
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
        }
    },
    websocketError: function(evt) {
        console.log("ws error " + JSON.stringify(evt));
    }
};

var oldHash = "";
var mapper = Object.create(null);

function peerClicked(peer)
{
    window.location.hash = '#detail-' + peer;
};

function init() {
    mapper[""] = document.getElementById('stats');
    mapper["detail"] = document.getElementById('detail');

    var url = "ws://" + window.location.hostname + ":" + window.location.port + "/";
    ws = new WebSocket(url);
    ws.onopen = callbacks.websocketOpen;
    ws.onclose = callbacks.websocketClose;
    ws.onmessage = callbacks.websocketMessage;
    ws.onerror = callbacks.websocketError;

    var canvas = document.getElementById('stats');
    paper.setup(canvas);
    common = new Common(canvas);
    mode = new Pie();
    mainmode = mode;

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

window.location.hash = "";
window.addEventListener("load", init, false);
