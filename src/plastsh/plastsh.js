/*global require, process console*/

var opts = {
    host: "127.0.0.1",
    port: 8089
};

var ws;
var readline = require('readline');
var websocket = require('ws');
var rl = readline.createInterface({
    completer: completer,
    input: process.stdin,
    output: process.stdout,
    terminal: true
});

// wow, node should really fix their shit
(function() {
    var oldStdout = process.stdout;
    var newStdout = Object.create(oldStdout);
    newStdout.write = function() {
        oldStdout.write('\x1b[2K\r');
        var result = oldStdout.write.apply(
            this,
            Array.prototype.slice.call(arguments)
        );
        rl.prompt();
        return result;
    };
    Object.defineProperty(process, 'stdout', {
        get: function stdout() { return newStdout; }
    });
})();

function setupWs()
{
    ws = new websocket('ws://' + opts.host + ':' + opts.port + '/');

    ws.on('message', function(data, flags) {
        try {
            var resp = JSON.parse(data);
        } catch (e) {
            console.log("Unable to parse " + data + " as JSON");
        }
        if (resp.error) {
            console.log("Error:", resp.error);
        } else {
            console.log(resp);
        }
    });
    ws.on('close', function() {
        console.log('ws socket closed');
        process.exit(0);
    });
    ws.on('error', function(e) {
        console.log('ws error', e);
        process.exit();
    });
}

function sendCommand(cmd, args)
{
    ws.send(JSON.stringify({ cmd: cmd, args: args }));
}

var handlers = {
    quit: function() {
        process.exit(0);
    },
    help: function() {
        console.log('help here');
    },
    peers: function() {
        sendCommand('peers');
    },
    test: function(args) {
        sendCommand('test', args);
    },
    block: function(args) {
        sendCommand('block', args);
    }
};

function handleCommand(cmd)
{
    var args = cmd.split(' ');
    var h = handlers[args.shift()];
    if (typeof h === 'function') {
        h(args);
    }
}

function completer(line) {
    var completions = [];
    for (var h in handlers) {
        completions.push('/' + h);
    }
    var hits = completions.filter(function(c) { return c.indexOf(line) == 0; });
    // show all completions if none found
    return [hits.length ? hits : completions, line];
}

(function(argv) {
    opts.host = argv.host || argv.h || opts.host;
    opts.port = parseInt(argv.port) || parseInt(argv.p) || opts.port;
})(require('minimist')(process.argv.slice(2)));

setupWs();

rl.setPrompt('plastsh> ');
rl.prompt();

rl.on('line', function(line) {
    if (line.length > 0 && line[0] === '/')
        handleCommand(line.substr(1));
    // else
    //     console.log(line);
    rl.prompt();
}).on('close', function() {
    process.exit(0);
});
