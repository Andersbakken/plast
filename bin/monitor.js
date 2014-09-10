function queryValue(key) {
    var search = window.location.search.substr(1).split('&');
    for (var i=0; i<search.length; ++i) {
        if (search[i].lastIndexOf(key + '=', 0) == 0) {
            return search[i].substr(key.length + 1);
        }
    }
    return undefined;
}

function onEventSource(msg)
{
    console.log("GOT MESSAGE", msg);
}

function start() {
    var server = queryValue("server");
    if (!server) {
        server = window.location.protocol + "//" + window.location.host + "/events";
    }
    console.log(server);
    // console.log(queryValue("server"));
    var eventSource = new EventSource(server);
    eventSource.onmessage = onEventSource;
}
