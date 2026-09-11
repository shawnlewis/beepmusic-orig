// Does a bunch of requests in parallel.

var http = require('http');

var id = 0;
function doReq() {
    var localId = id++;
    console.log('starting ' + localId);

    var dataLen = 0;

    var req = http.request(
        'http://www.stephaniequinn.com/Music/Commercial%20DEMO%20-%2005.mp3',
        function(res) {
            res.on('data', function(buf) {dataLen += buf.length});
            res.on('close', function() {console.log('closed ' + localId + ' total ' + dataLen)});
        });
    req.on('error', function() {console.log('error ' + localId)});
    req.end();
}

for (var i=0; i<15; i++) {
    setTimeout(doReq, i * 1000);
}
