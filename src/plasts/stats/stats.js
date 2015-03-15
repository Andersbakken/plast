/*global paper, console, JSON, WebSocket*/

var ws, scheduler;
var mode, mainmode, common;
var colors = [ "red", "blue", "yellow", "green", "cyan" ];
var unusedColor = "purple";
var curColor = 0;
var peerColors = Object.create(null);

function generateColor(peer)
{
    peerColors[peer] = colors[curColor++ % colors.length];
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
    width: function() {
        return this.stats.width / ((window.devicePixelRatio || 1) / this._pixelRatio);
    },
    height: function() {
        return this.stats.height / ((window.devicePixelRatio || 1) / this._pixelRatio);
    }
};

function Pie(args)
{
};

Pie.prototype = {
    _peers: Object.create(null),
    _totalJobs: 0,
    _root: undefined,
    _using: Object.create(null),
    _running: Object.create(null),

    constructor: Pie,
    processMessage: function(msg) {
        if (msg.type === "peer") {
            if (msg.delete) {
                if (msg.id in this._peers) {
                    this._totalJobs -= this._peers[msg.id].jobs;
                    delete this._peers[msg.id];
                }
            } else {
                this._peers[msg.id] = msg;
                this._totalJobs += msg.jobs;
                generateColor(msg.name);
            }
            this._redraw();
        } else if (msg.type === "build") {
            if (msg.start) {
                if (msg.jobid in this._running) {
                    ++this._running[msg.jobid].count;
                } else {
                    this._running[msg.jobid] = { count: 1, peer: msg.peer };
                }
                if (!(msg.peer in this._using)) {
                    this._using[msg.peer] = 1;
                } else {
                    this._using[msg.peer] += 1;
                }
            } else if (msg.jobid in this._running) {
                var peername = this._running[msg.jobid].peer;
                if (peername in this._using) {
                    this._using[peername] -= 1;
                    if (this._using[peername] === 0) {
                        delete this._using[peername];
                    }
                }
                if (!--this._running[msg.jobid].count)
                    delete this._running[msg.jobid];
            }
            this._redraw();
        } else {
            console.log("unhandled message", msg);
        }
    },

    _clicked: function(peer) {
        window.location.hash = '#detail-' + peer;
    },
    _redraw: function() {
        if (this._root)
            this._root.remove();
        this._root = new paper.Group();
        // make a pie
        var radius = Math.min(common.width(), common.height()) / 3;
        if (Object.keys(this._using).length === 0) {
            // noone is building, make a circle
            var circle = new paper.Path.Circle(common.center(), radius);
            circle.fillColor = unusedColor;
            this._root.addChild(circle);
        } else {
            // make arcs
            var c = common.center();
            var pt = new paper.Point(c.x + radius, c.y);
            var diff = (Math.PI * 2) / this._totalJobs;
            var used = 0, ta, using, end, through, arc, label, labelTurn, labelPosition, cur = 0;
            var that = this;
            for (var peer in this._using) {
                using = this._using[peer];
                ta = cur + (using * diff / 2);
                through = new paper.Point(c.x + (radius * Math.cos(ta)),
                                          c.y + (radius * Math.sin(ta)));
                cur += using * diff;
                end = new paper.Point(c.x + (radius * Math.cos(cur)),
                                      c.y + (radius * Math.sin(cur)));

                arc = new paper.Path(c);
                arc.add(pt);
                arc.arcTo(through, end);
                arc.closePath();
                arc.fillColor = peerColors[peer];
                arc.onClick = function() { that._clicked(peer); };
                this._root.addChild(arc);

                // label
                labelTurn = new paper.Point(1.25 * radius * Math.cos(ta) + c.x,
                                            1.25 * radius * Math.sin(ta) + c.y);
                if (labelTurn.x >= c.x) { // turn right
                    labelPosition = new paper.Point(labelTurn.x + 15, labelTurn.y);
                } else {
                    labelPosition = new paper.Point(labelTurn.x - 15, labelTurn.y);
                }
                label = new paper.PointText(labelPosition);
                label.content = peer;
                label.fillColor = "black";
                this._root.addChild(label);

                pt = end;
                used += using;
            }
            if (used < this._totalJobs) {
                // make one final arc
                using = this._totalJobs - used;
                ta = cur + (using * diff / 2);
                through = new paper.Point(c.x + (radius * Math.cos(ta)),
                                          c.y + (radius * Math.sin(ta)));
                cur += using * diff;
                end = new paper.Point(c.x + (radius * Math.cos(cur)),
                                      c.y + (radius * Math.sin(cur)));
                cur += using * diff;

                arc = new paper.Path(c);
                arc.add(pt);
                arc.arcTo(through, end);
                arc.closePath();
                arc.fillColor = unusedColor;

                this._root.addChild(arc);
            }
        }
        paper.view.draw();
    }
};

function Detail(args)
{
    this.local = args.name;
    this.dom = document.getElementById('detail');
    while (this.dom.firstChild) {
        this.dom.removeChild(this.dom.firstChild);
    }
    this._table = document.createElement('table');
    this.dom.appendChild(this._table);
};

Detail.prototype = {
    constructor: Detail,
    _table: undefined,
    _map: Object.create(null),
    processMessage: function(msg) {
        if (msg.type !== "build")
            return;
        if (msg.local !== this.local)
            return;
        if (msg.start) {
            // insert
            var row = this._table.insertRow(-1);
            this._addColumn(row, msg.file);
            this._addColumn(row, msg.peer);
            this._map[msg.jobid] = row;
        } else {
            if (!(msg.jobid in this._map))
                return;
            this._table.deleteRow(this._map[msg.jobid].rowIndex);
            delete this._map[msg.jobid];
        }
        //console.log("message for local:", msg);
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
    return { dom: mapper[prefix], data: { prefix: prefix, name: hash.substr(dash + 1) } };
}

function update(data)
{
    if (!data) {
        mode = mainmode;
    } else if (data.prefix == "detail") {
        mode = new Detail({ name: data.name });
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
