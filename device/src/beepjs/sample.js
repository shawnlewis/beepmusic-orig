// Http/BeepLib test.
var http = require('http');
var https = require('https');

var player = beepjs.natives.beeplib.BeepLib();
player.init();
player.addRemote('127.0.0.1:12948');

var web_opts = {
    hostname: 'www.google.com',
    path: '',
    method: 'GET'
};

var song_opts = {
    hostname: '127.0.0.1',
    port: 8000,
    path: 'test.mp3',
    method: 'GET'
};

var bad_opts = {
    hostname: '169.254.0.1',
    port: 1
};

var web_req = https.get(web_opts, function(res) {
    res.setEncoding('utf8');
    res.on('data', function(chunk) {
        console.log('chunk: ' + chunk);
    });
});

var song_req = http.request(song_opts, function(res) {
    player.stBegin();
    res.on('data', function(buf) {
        player.buffer(buf);
    });
    res.on('close', function() {
        player.stEnd();
    });
});

var bad_req = http.request(bad_opts, function(res) {});
bad_req.on('error', function(e) {
    console.log('problem with request: ' + e.message);
});

song_req.end();
bad_req.end();
