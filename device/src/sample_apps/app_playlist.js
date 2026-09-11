var url = require('url');
var http = require('http');
var cast = require('cast');

// fetches a url and returns the result as a single string.
function fetchUrl(url, onSuccess, onError) {
    var data = [];
    var req = http.request(url, function(result) {
        console.log("HERE");
        result.setEncoding('utf8');
        result.on('data', function(buf) {
            data.push(buf);
        });
        result.on('close', function() {
            if (data.length === 0) {
                onError();
            } else {
                onSuccess(data.join(''));
            }
        });
    });
    req.on('error', function() {
        console.log("ERROR");
        onError()
    });
    req.end();
}

// parse a playlist, which is a list of urls of media files, one per line.
function parsePlaylist(playlistString) {
    var lines = playlistString.split('\n');
    var pl = [];
    for (var i=0; i<lines.length; i++) {
        if (lines[i] === '') {
            continue;
        }
        var parsed = url.parse(lines[i]);
        if (!parsed.host || !parsed.slashes || !parsed.protocol) {
            return null;
        }
        pl.push(parsed);
    }
    return pl;
}

// Cast setup.
var appConfig = new cast.receiver.BeepReceiverManager.Config();
appConfig.appName = 'sample_playlist';
var manager = cast.receiver.BeepReceiverManager.getInstance();
manager.start(appConfig);
var audio = cast.getAudioInstance();
var mediaManager = new cast.receiver.MediaManager(audio);

// global state
var playlist = null;
var playlistIndex = 0;


////// Handle system events

manager.onSkipRequested = function(ev) {
    return playNextPlaylistItem();
}

manager._origOnPlayRequested = manager.onPlayRequested;
manager.onPlayRequested = function(ev) {
    console.log('play requested, calling original play.');
    manager._origOnPlayRequested();
}

manager._origOnPauseRequested = manager.onPauseRequested;
manager.onPauseRequested = function(ev) {
    console.log('pause requested, calling original pause.');
    manager._origOnPauseRequested();
}

manager.onSystemVolumeChanged = function(ev) {
    console.log('VOLUME CHANGE: ' + ev.data);
}

///// End handling system events


// Override mediamanager onLoad to load a playlist instead of a single
// media file.
mediaManager.onLoad = function(ev) {
    var message = ev.data;
    var playlistUrl = message.media.contentId;
    fetchUrl(playlistUrl,
            function(result) {  // onSuccess
                manager.setStation('playlist: ' + playlistUrl);
                playlist = parsePlaylist(result);
                if (playlist === null) {
                    console.log('Playlist parse error.');
                    mediaManager.sendLoadError('PLAYLIST_PARSE');
                } else if (playlist.length === 0) {
                    console.log('Empty playlist');
                    mediaManager.sendLoadError('PLAYLIST_EMPTY');
                } else {
                    playlistIndex = -1;
                    playNextPlaylistItem();
                    mediaManager.sendLoadComplete();
                }
            },
            function() { // onError
                console.log('Couldn\'t fetch playlist');
                mediaManager.sendLoadError('PLAYLIST_FETCH');
            });
}

// This shows how to create a custom message bus to receive custom messages.
// Here we implement a SKIP function. We also use this bus to send
// a message whenever we play a new URL.
var mbus = manager.getCastMessageBus('playlist');
mbus.onMessage = function(event) {
    console.log('GOT MESSAGE: ' + event.data);
    if (event.data == 'SKIP') {
        playNextPlaylistItem();
    }
}

function playNextPlaylistItem() {
    if (playlist === null) {
        return false;
    }
    playlistIndex++;
    if (playlistIndex >= playlist.length) {
        // TODO: notify app
        console.log('Playlist ended');
        return false;
    }
    console.log('Playing next playlist entry.');
    audio.setNextSrc(playlist[playlistIndex].href,
            {'artist': '',
             'albumName': 'number: ' + playlistIndex,
             'title': playlist[playlistIndex].href});

    // Send a custom message saying what url we are playing.
    mbus.broadcast('Playing: ' + playlist[playlistIndex].href);

    return true;
}

// When the current track ends, advance to the next one.
audio.addEventListener('ended', function() {
    playNextPlaylistItem();
});

