/*global paper, console, JSON, WebSocket*/

var ws, scheduler;
var mode;

var peers = {
    init: function(canvas) {
        var ctx = canvas.getContext("2d");
        this._canvas = canvas;
        this._pixelRatio = ctx.webkitBackingStorePixelRatio
            || ctx.mozBackingStorePixelRatio
            || ctx.msBackingStorePixelRatio
            || ctx.oBackingStorePixelRatio
            || ctx.backingStorePixelRatio
            || 1;
    },
    add: function(peer) {
        this._peers[peer.id] = peer;
    },
    remove: function(id) {
        if (this._peers.hasOwnProperty(id)) {
            this._peers[id].invalidate();
            delete this._peers[id];
        }
    },
    get: function(name) {
        for (var i in this._peers) {
            if (this._peers[i].name === name)
                return this._peers[i];
        }
        return undefined;
    },
    center: function(rel) {
        var pt = new paper.Point(this.width() / 2, this.height() / 2);
        if (rel) {
            pt.x -= rel.width / 2;
            pt.y -= rel.height / 2;
        }
        return pt;
    },
    peerSize: function() {
        return new paper.Size(this.width() / 10, this.height() / 20);
    },
    roundSize: function() {
        return new paper.Size(this.width() / 100, this.height() / 100);
    },
    recalculate: function() {
        var sz = this.peerSize();
        var origo = this.center(sz);
        scheduler.recalculate(origo, sz);

        // put all peers on the same radius for now
        var r = Math.min(this.width(), this.height()) / 4;
        var diff = (Math.PI * 2) / Object.keys(this._peers).length;
        var cur = 0;
        for (var i in this._peers) {
            var pt = new paper.Point(origo.x + (r * Math.cos(cur)),
                                     origo.y + (r * Math.sin(cur)));
            this._peers[i].recalculate(pt, sz);
            cur += diff;
        }
    },
    draw: function() {
        scheduler.draw();
        for (var i in this._peers) {
            this._peers[i].draw();
        }
        paper.view.draw();
    },
    width: function() {
        return this._canvas.width / ((window.devicePixelRatio || 1) / this._pixelRatio);
    },
    height: function() {
        return this._canvas.height / ((window.devicePixelRatio || 1) / this._pixelRatio);
    },
    processMessage: function(msg) {
        console.log("ws msg", msg);
        if (msg.type !== "build")
            return;
        var peer;
        if (msg.start) {
            if ((peer = this.get(msg.local))) {
                peer.jobs.add(peer, msg);
                this._messages[msg.jobid] = msg.local;
            }
        } else {
            var peername = this._messages[msg.jobid];
            delete this._messages[msg.jobid];
            if ((peer = this.get(peername))) {
                peer.jobs.remove(peer, msg);
            }
        }
        this.draw();
    },
    _canvas: undefined,
    _peers: {},
    _messages: Object.create(null),
    _pixelRatio: undefined
};

function Peer(args) {
    this.id = args.id;
    this.name = args.name;
    this.color = args.color;
    this._jobs = Object.create(null);
}

Peer.prototype = {
    JOB_OFFSET: 1,
    constructor: Peer,
    rect: undefined,
    text: undefined,
    _path: undefined,
    _group: undefined,
    _jobs: undefined,
    _adjustRect: function(off) {
        return new paper.Rectangle(this.rect.x - off, this.rect.y - off,
                                   this.rect.width + off * 2, this.rect.height + off * 2);
    },
    draw: function() {
        // sort jobs based on local
        var sortedJobs = Object.create(null), job, local;
        for (job in this._jobs) {
            local = this._jobs[job].local;
            if (!(local in sortedJobs)) {
                sortedJobs[local] = [];
            }
            sortedJobs[local].push(this._jobs[job]);
        }

        this.invalidate();
        this._path = new paper.Path.RoundRectangle(this.rect, peers.roundSize());
        this._path.fillColor = this.color;
        this._text = new paper.PointText(this.rect.center);
        this._text.content = this.name;
        this._text.style = { fontSize: 15, fillColor: "white", justification: "center" };
        //this._path.insertBelow(this._text);
        this._group = new paper.Group();

        var numSorted = Object.keys(sortedJobs).length;
        var off = 0;
        for (local in sortedJobs) {
            var jobs = sortedJobs[local];
            var num = jobs.length;
            off += (num * Peer.prototype.JOB_OFFSET);
            var other = peers.get(local);
            // add a rect
            var rect = new paper.Path.RoundRectangle(this._adjustRect(off), peers.roundSize());
            rect.fillColor = other.color;
            this._group.addChild(rect);
        }

        this._group.addChild(this._path);
        this._group.addChild(this._text);
        this._group.onClick = onPeerClick;
        return this;
    },
    invalidate: function() {
        if (this._group) {
            var chld = this._group.children;
            for (var idx in chld) {
                chld[idx].remove();
            }
        }
    },
    recalculate: function(pos, size) {
        this.rect = new paper.Rectangle(pos, new paper.Point(pos.x + size.width, pos.y + size.height));
    },
    jobs: {
        add: function(peer, job) {
            peer._jobs[job.jobid] = job;
        },
        remove: function(peer, job) {
            delete peer._jobs[job.jobid];
        }
    }
};

function onPeerClick(event) {
    window.location.hash = '#detail-' + this.children[1].content;
    console.log(this.children[1].content);
}

function handlePeer(peer)
{
    if (peer["delete"]) {
        peers.remove(peer.id);
    } else {
        var p = new Peer({ id: peer.id, name: peer.name, color: "blue" });
        peers.add(p);
    }
    peers.recalculate();
    peers.draw();
}

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
        try {
            var obj = JSON.parse(evt.data);
            if (obj.type === "peer")
                handlePeer(obj);
            if (mode) {
                mode.processMessage(obj);
            } else {
                peers.processMessage(obj);
            }
        } catch (e) {
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
    peers.init(canvas);

    scheduler = new Peer({ id: 0, name: "scheduler", color: "red" });

    paper.setup(canvas);
    paper.view.onResize = function(event) {
        peers.recalculate();
        peers.draw();
    };

    peers.recalculate();
    peers.draw();
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
        mode = undefined;
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
