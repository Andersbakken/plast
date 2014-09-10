/*global document, clearInterval, setInterval, EventSource*/
function queryValue(key) {
    var search = window.location.search.substr(1).split('&');
    for (var i=0; i<search.length; ++i) {
        if (search[i].lastIndexOf(key + '=', 0) == 0) {
            return search[i].substr(key.length + 1);
        }
    }
    return undefined;
}

var skeletonCount = 0;
var skeletons = {};

function windowWidth()
{
    var x = 0;
    if (self.innerHeight) {
        x = self.innerWidth;
    } else if (document.documentElement && document.documentElement.clientHeight) {
        x = document.documentElement.clientWidth;
    } else if (document.body) {
        x = document.body.clientWidth;
    }
    return x;
}

function onEventSource(msg)
{
    console.log(msg.data);
    var message = JSON.parse(msg.data);
    switch (message.type) {
    case 'start':
        var skel = new Skeleton;
        skel.move(undefined, 10 + (skeletonCount * 200));
        var targetX = windowWidth() - 50;
        console.log("WALKING TO", targetX);
        skel.walk(targetX, undefined, 20000);
        skeletons[msg.id] = skel;
        ++skeletonCount;
        break;
    case 'end':
        skeletons[msg.id].fadeOut(1000, function() {
            delete skeletons[msg.id];
            --skeletonCount;
        });
        break;
    default:
        console.log("Unknown message type", message);
        break;
    }
}

function interpolate(begin, end, startTime, duration) {
    var elapsed = new Date() - startTime;
    var progress;
    if (elapsed >= duration) {
        progress = 1;
    } else {
        progress = elapsed / duration;
    }
    return begin + ((end - begin) * progress);
}

function Skeleton()
{
    var img = document.createElement("img");
    img.src = "cool-skeleton-w.gif";
    img.style.position = "absolute";
    var area = document.getElementById("area");
    area.appendChild(img);
    var animationTimer, startX, targetX, startY, targetY, animationDuration, animationStart;
    function advance(self) {
        var x = typeof startX === 'undefined' ? undefined : interpolate(startX, targetX, animationStart, animationDuration);
        var y = typeof startY === 'undefined' ? undefined : interpolate(startY, targetY, animationStart, animationDuration);
        self.move(x, y);
        return (new Date() >= animationStart + animationDuration);
    }

    return {
        move: function(x, y) {
            if (typeof x === 'number')
                img.style.left = x + "px";
            if (typeof y === 'number')
                img.style.top = y + "px";
            // img.style="position:absolute; left:" + x + "px; top:" + y + "px;";
        },
        get x() { return img.x; },
        get y() { return img.y; },
        kill: function() {
            area.removeChild(img);
        },
        walk: function(x, y, time) {
            if (animationTimer) {
                clearInterval(animationTimer);
            }
            if (typeof x === 'number') {
                startX = img.x;
                targetX = x;
            }
            if (typeof y === 'number') {
                startY = img.y;
                targetY = y;
            }
            animationDuration = time || 10000;
            animationStart = new Date();
            var that = this;
            animationTimer = setInterval(function() {
                if (advance(that)) {
                    clearInterval(animationTimer);
                    animationTimer = undefined;
                }
            }, 16);
        },
        fadeOut: function(time, cb) {
            var start = new Date();
            if (!time)
                time = 5000;
            var that = this;
            setInterval(function() {
                img.style.opacity = interpolate(1.0, 0.0, start, time);
                if (new Date() >= start + time) {
                    that.kill();
                    if (cb)
                        cb();
                }
            }, 16);

        },
        get node() { return img; },
        get area() { return area; }
    };
    // document.getElementById('carRight').style="position:absolute; left:100px; top:170px;";
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

    // window.skel = new Skeleton;
}
