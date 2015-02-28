/*global require, process console*/

var readline = require('readline');
var ws = require('ws');

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
