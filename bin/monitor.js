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

function onEventSource(msg)
{
    console.log("GOT MESSAGE", msg);
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
        var elapsed = new Date() - animationStart;
        var progress;
        if (elapsed >= animationDuration) {
            progress = 1;
        } else {
            progress = elapsed / animationDuration;
        }

        function calc(start, end) { return start + ((end - start) * progress); }
        var x = typeof startX === 'undefined' ? undefined : calc(startX, targetX);
        var y = typeof startY === 'undefined' ? undefined : calc(startY, targetY);
        self.move(x, y);
        return progress == 1;
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

    window.skel = new Skeleton;
}
