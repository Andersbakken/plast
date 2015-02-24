/*global paper, console, JSON, WebSocket*/

var ws, scheduler;

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
        delete this._peers[id];
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
    _canvas: undefined,
    _peers: {},
    _pixelRatio: undefined
};

function Peer(args) {
    this.id = args.id;
    this.name = args.name;
    this.color = args.color;
}

Peer.prototype = {
    constructor: Peer,
    rect: undefined,
    text: undefined,
    draw: function() {
        if (this._path)
            this._path.remove();
        this._path = new paper.Path.RoundRectangle(this.rect, peers.roundSize());
        this._path.fillColor = this.color;
        if (this._text)
            this._text.remove();
        this._text = new paper.PointText(this.rect.center);
        this._text.content = this.name;
        this._text.style = { fontSize: 15, fillColor: "white", justification: "center" };
        this._path.insertBelow(this._text);
        return this;
    },
    recalculate: function(pos, size) {
        this.rect = new paper.Rectangle(pos, new paper.Point(pos.x + size.width, pos.y + size.height));
    }
};

function handlePeer(peer)
{
    if (peer["delete"]) {
        peers.remove(peer.id);
    } else {
        var p = new Peer({ id: peer.id, name: peer.name, color: "blue" });
        peers.add(p);
        peers.recalculate();
        peers.draw();
    }
}

var callbacks = {
    websocketOpen: function(evt) {
        console.log("ws open");
        ws.send("hello there");
    },
    websocketClose: function(evt) {
        console.log("ws close");
    },
    websocketMessage: function(evt) {
        console.log("ws msg " + evt.data);
        try {
            var obj = JSON.parse(evt.data);
            if (obj.type === "peer")
                handlePeer(obj);
        } catch (e) {
        }
    },
    websocketError: function(evt) {
        console.log("ws error " + JSON.stringify(evt));
    }
};

function init() {
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

window.addEventListener("load", init, false);
