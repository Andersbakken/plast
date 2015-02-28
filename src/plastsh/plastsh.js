/*global require, process console*/

var opts = {
    host: "127.0.0.1",
    port: 8089
};

var readline = require('readline');
var websocket = require('ws');
var ws;

function setupWs()
{
    ws = new websocket('ws://' + opts.host + ':' + opts.port + '/');

    ws.on('message', function(data, flags) {
        console.log('ws msg', data);
        rl.prompt();
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

var handlers = {
    quit: function() {
        process.exit(0);
    },
    help: function() {
        console.log('help here');
    }
};

function handleCommand(cmd)
{
    var h = handlers[cmd];
    if (typeof h === 'function')
        h();
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

var rl = readline.createInterface({
    completer: completer,
    input: process.stdin,
    output: process.stdout
});

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
    else
        console.log(line);
    rl.prompt();
}).on('close', function() {
    process.exit(0);
});
