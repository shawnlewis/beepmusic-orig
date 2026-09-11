// Note: you should not use more than one of these at once, because of
// the way BeepApp is implemented, but there is no enforcement of this
// rule.

var beep = require('beep');
var http = require('http');
var util = require('util');
var EventEmitter = require('events').EventEmitter;

function parseUrl(url) {
    var ret = {};
    var sep_pos = url.search('://');
    if (sep_pos === -1) {
        return null;
    }
    ret.scheme = url.substr(0, sep_pos);
    var rest = url.substr(sep_pos + 3);
    sep_pos = rest.indexOf('/');
    if (sep_pos === -1) {
        return null;
    }
    ret.host = rest.substr(0, sep_pos);
    ret.path = rest.substr(sep_pos);
    return ret;
}


// Streams media from url to beep audio
// emits events:
//   success
//   fetchError
//   tokenLost
function HttpFileStream(app, url, metadata) {
    this.app = app;
    this.url = url;
    this.metadata = metadata || {};
    this.parsed_url = parseUrl(url);
    this._cur_song_req = null;
    this._buffer_poll_interval = null;
    this._closePending = false;
}
util.inherits(HttpFileStream, EventEmitter);

///// private

HttpFileStream.prototype.abort = function() {
    console.log('Aborting HttpFileStream');
    if (this._cur_song_req) {
        this._cur_song_req.abort();
    }
}


HttpFileStream.prototype._onLostToken = function() {
    console.log('Token lost.');
    this.abort();
    this.emit('tokenLost');
}

HttpFileStream.prototype.onReqData = function(buf) {
    if (this._buffer_poll_interval) {
        console.error('Got onReqData when we already have a pending buffer!');
        return;
    }
    var can_buf = this.app.audioCanBuffer(buf.length);
    if (can_buf == 1) {
        if (!this.app.audioBuffer(buf, buf.length)) {
            this._onLostToken();
        }
    } else if (can_buf == 0) {
        var temp_buf = new Buffer(buf);
        var temp_buf_len = buf.copy(temp_buf);

        this._cur_song_req.pause();

        var self = this;
        this._buffer_poll_interval = setInterval(function() {
            if (self._bufferPoll(temp_buf, temp_buf_len)) {
                clearInterval(self._buffer_poll_interval);
                self._buffer_poll_interval = null;
                if (self._closePending) {
                    self._doClose();
                }
            }
        }, 2000);
    } else if (can_buf == -1) {
        this._onLostToken();
    }
}

HttpFileStream.prototype._bufferPoll = function(buf, buf_len) {
    if (!this._cur_song_req || this._cur_song_req.aborted) {
        return true;
    }
    var poll_can_buf = this.app.audioCanBuffer(buf_len);
    if (poll_can_buf != 0) {
        if (poll_can_buf == 1) {
            if (!this.app.audioBuffer(buf, buf_len)) {
                this._onLostToken();
            } else {
                this._cur_song_req.resume();
            }
        }
        if (poll_can_buf == -1) {
            this._onLostToken();
        }
        return true;
    }
    return false;
}

HttpFileStream.prototype._doClose = function() {
    if (!this._cur_song_req.aborted) {
        if (!this.app.audioTrackEnd()) {
            this._onLostToken();
        } else {
            this.emit('success');
        }
    }
    this._closePending = false;
    this._cur_song_req = null;
}

HttpFileStream.prototype.onReqClose = function() {
    if (this._bufferPollInterval) {
        this._closePending = true;
    } else {
        this._doClose();
    }
}

HttpFileStream.prototype.getMetadata = function() {
    var metadata = {}
    metadata.artist = this.metadata.artist || 'Unknown';
    metadata.albumName = this.metadata.albumName || 'Unknown';
    metadata.title = this.metadata.title || 'Unknown';
    metadata.images = this.metadata.images;
    if (!metadata.images || metadata.images.length === 0) {
        metadata.images = [''];
    }
    return metadata;
}

///// public

HttpFileStream.prototype.start = function() {
    beepjs.natives.system.GC();
    if (this.parsed_url == -1) {
        this.emit('fetchError');
        return;
    }

    // TODO: We're dropping the scheme (http/https)
    var opts = {
        hostname: this.parsed_url.host,
        path: this.parsed_url.path
    };
    var metadata = this.getMetadata();
    var self = this;
    this._cur_song_req = http.request(opts, function(res) {
        var content_length = 0;
        if (res.headers['content-length']) {
            content_length = parseInt(res.headers['content-length']);
        }
        if (!self.app.audioTrackBegin(
                metadata.title, metadata.artist,
                metadata.albumName, metadata.title,
                metadata.images[0],
                'mp3', content_length)) {
            self._onLostToken();
        }
        res.on('data', function(buf) {
            self.onReqData(buf);
        });
        res.on('close', function() {
            self.onReqClose();
        });
    });
    this._cur_song_req.on('error', function(e) {
        self.emit('fetchError');
    });
    this._cur_song_req.end();
};

exports.HttpFileStream = HttpFileStream;
