var util = require('util');

var beep = require('beep');
var HttpFileStream = require('stream').HttpFileStream;

var app = null;

function setApp(_app) {
    app = _app;
}

function HttpFileStreamFactory(app, url, metadata) {
    return new HttpFileStream(app, url, metadata);
}

var httpFileStreamFactory = HttpFileStreamFactory;

function notImplemented(func, extra) {
    console.warn(func + ' is not currently implemented on Beep. '
            + extra);
}

function Audio() {
    this._stream = null;
    this._listeners = {};
    this._currentTime = 0;
    this._paused = true;
    this._src;

    this._trackVolumeGainRaw = 1.0;
    this._trackVolumeGain = 1000;

    this._gotInitialState = false;

    ///// properties

    this.duration = NaN;
    this.ended = false;

    this.__defineGetter__('audioTracks', function() {
        notImplemented('audio.audioTracks property');
    });

    this.__defineSetter__('autoplay', function(val) {
        notImplemented('setting audio.autoplay',
            'audio.autoplay is always true');
    });
    this.__defineGetter__('autoplay', function() {
        return true;
    });

    this.__defineGetter__('buffered', function() {
        notImplemented('audio.buffered property');
    });

    this.__defineGetter__('controller', function() {
        notImplemented('audio.controller property');
    });

    this.__defineGetter__('controller', function() {
        notImplemented('audio.controller property');
    });

    this.__defineSetter__('controls', function() {
        notImplemented('audio.controls property');
    });
    this.__defineGetter__('controls', function() {
        notImplemented('audio.controls property');
    });

    this.__defineSetter__('crossOrigin', function() {
        notImplemented('audio.crossOrigin property');
    });
    this.__defineGetter__('crossOrigin', function() {
        notImplemented('audio.crossOrigin property');
    });

    this.__defineGetter__('currentSrc', function() {
        if (this._src) {
            return this._src;
        } else {
            return '';
        }
    });

    this.__defineSetter__('currentTime', function() {
        notImplemented('setting audio.currentTime property');
    });
    this.__defineGetter__('currentTime', function() {
        return this._currentTime;
    });

    this.__defineSetter__('defaultMuted', function() {
        notImplemented('setting audio.defaultMuted property');
    });
    this.__defineGetter__('defaultMuted', function() {
        return false;
    });

    this.__defineSetter__('defaultPlaybackRate', function() {
        notImplemented('setting audio.defaultPlaybackRate property');
    });
    this.__defineGetter__('defaultPlaybackRate', function() {
        return 1.0;
    });

    this.__defineGetter__('error', function() {
        notImplemented('audio.error property');
    });

    // TODO: implement loop
    this.__defineSetter__('loop', function() {
        notImplemented('setting audio.loop property');
    });
    this.__defineGetter__('loop', function() {
        return false;
    });

    this.__defineGetter__('mediaGroup', function() {
        notImplemented('audio.mediaGroup property');
    });

    this.__defineSetter__('muted', function() {
        notImplemented('setting audio.muted property');
    });
    this.__defineGetter__('muted', function() {
        return false;
    });

    this.__defineGetter__('networkState', function() {
        // TODO: implement networkState
        notImplemented('audio.networkState property');
    });

    this.__defineSetter__('paused', function(val) {
        if (val) {
            this.pause();
        } else {
            this.play();
        }
    });

    this.__defineGetter__('paused', function() {
        return this._paused;
    });

    this.__defineSetter__('playbackRate', function() {
        notImplemented('setting audio.playbackRate property');
    });
    this.__defineGetter__('playbackRate', function() {
        return 1.0;
    });

    this.__defineGetter__('played', function() {
		return {
			length: 1,
			start: function(i) {
				return 0;
			},
			end: function(i) {
				return this.currentTime;
			}
		}
    });

    this.__defineSetter__('preload', function() {
        notImplemented('setting audio.preload property');
    });
    this.__defineGetter__('preload', function() {
        return 'auto';
    });

    this.__defineGetter__('readyState', function() {
		return 4;
        notImplemented('audio.readyState property');
    });

    this.__defineGetter__('seekable', function() {
        notImplemented('audio.seekable property');
    });

    this.__defineGetter__('seeking', function() {
        return false;
    });

    // Beep extension
    this.__defineSetter__('trackVolumeGain', function(val) {
        if (val < 0) {
            val = 0;
        } else if (val > 1.0) {
            val = 1.0;
        }

        // When we convert val to an integer we lose any precision
        // past 3 decimels.  Preserve the raw value for future calls
        // to trackVolumeGain getter to avoid value drift
        this._trackVolumeGainRaw = val;
        this._trackVolumeGain = Math.round(val * 1000);
        app.setTrackVolumeScalar(this._trackVolumeGain);
    });

    this.__defineGetter__('trackVolumeGain', function() {
        return this._trackVolumeGainRaw;
    });

    this.__defineSetter__('src', function(val) {
        console.warn('NOTE: Setting the src property is not allowed, please'
                + ' use the setNextSrc method instead');
    });
    this.__defineGetter__('src', function() {
        return this._src;
    });

    this.__defineGetter__('startDate', function() {
        notImplemented('audio.startDate property');
    });

    this.__defineGetter__('textTracks', function() {
        notImplemented('audio.textTracks property');
    });

    this._volume = 0;
    this.__defineSetter__('volume', function(val) {
        this._volume = val;
        app.setVolume(Math.round(this._volume * 1000));
    });
    this.__defineGetter__('volume', function() {
        return this._volume;
    });

    var self = this;
    app.subscribe('beep.state.distributor._local_', function(target, message) {
        self._handleDistributorUpdate(JSON.parse(message));
    });
    app._onUbus_audio_ended = function(req) {
        // Happens if we don't queue up more tracks in distributor.
        self.ended = true;
        self._triggerEvent('ended');
        return beep.ubus_success();
    }
}

Audio.prototype._doSrcChange = function(metadata, optional_enqueue) {
    // TODO: Should this stop audio? Do we want to give clients a way
    // to stop audio? How can we ensure that music always plays when
    // using a preset or resume?
    if (!this._src) {
        return;
    }

    this.ended = false;
    this._setPaused(false);

    // acquire audio
    app.audioAcquire();
    if (app.token == -1) {
        // TODO: what we do here? end session?
        throw new Error('Could not acquire token');
    }
    this._currentTime = 0;
    this.duration = NaN;

    // app.audioAcquire() causes trackVolumeScalar to be reset to max
    // Apps that utilize the trackVolumeGain property should explicitly
    // set this to 1 if they want to retain that behavior.
    app.setTrackVolumeScalar(this._trackVolumeGain);

    if (this._station) {
        app.setStation(this._station.id,
                this._station.name,
                this._station.imageUrl,
                this._station.presetMethod,
                JSON.stringify(this._station.presetInfo));
    }
    app.audioResume();

    if (this._stream) {
        this._stream.abort();
        this._stream.removeAllListeners();
    }

    this._stream = httpFileStreamFactory(app, this._src, metadata);

    var self = this;
    this._stream.on('success', function() {
        console.log('Audio fully buffered');
        self._triggerEvent('canplaythrough');
    });
    this._stream.on('fetchError', function() {
        console.log('Error fetching audio');
        self._triggerEvent('error');
    });
    this._stream.on('tokenLost', function() {
        // TODO: how do we notify the user of this?
        console.log('tokenLost');
    });
    this._stream.start();

    this._triggerEvent('loadstart');

    this.duration = Infinity;

    // We don't fire the rest of the events until we receive
    // the duration_change event, which we're guaranteed to receive after
    // starting a new stream.
}

Audio.prototype._triggerEvent = function(ev) {
    var handlers = this._listeners[ev];
    if (handlers) {
        for (var i=0; i<handlers.length; i++) {
            handlers[i].call(this);
        }
    }
}

Audio.prototype._setInitialState = function(state) {
    this._setVolume(state.master_volume);
}

Audio.prototype._handleDistributorUpdate = function(message) {
    var event_type = message.event_type;
    var state = message.state;

    if (!event_type || !state) {
        console.error('Received invalid Beep audio state');
        return;
    }
    if (!this._gotInitialState) {
        this._gotInitialState = true;
        this._setInitialState(state);
    }

    if (event_type == 'duration_change') {
        if (state.duration != 0) {
            this.duration = state.duration;
        }
        this._triggerEvent('durationchange');
        this._triggerEvent('loadedmetadata');

        this._triggerEvent('loadeddata');
        this._triggerEvent('progress');
        this._triggerEvent('canplay');
    }
    if (event_type === 'progress') {
        this._currentTime = state.track_elapsed_secs;
        this._triggerEvent('timeupdate');
    } else if (event_type == 'audio_state_change') {
        if (state.audio_state == 'playing' || state.audio_state == 'working') {
            this._setPaused(false);
        } else {
            this._setPaused(true);
        }
    } else if (event_type == 'volume_changed') {
        this._setVolume(state.master_volume);
    }
}

Audio.prototype._setVolume = function(volume) {
    this._volume = volume / 1000;
    this._triggerEvent('volumechange');
    if (this.specialVolumeChangeHandler) {
        this.specialVolumeChangeHandler();
    }
}

// Returns true if paused changed.
Audio.prototype._setPaused = function(paused) {
    var previousPaused = this._paused;
    this._paused = paused;
    if (this._paused != previousPaused) {
        if (this._paused) {
            this._triggerEvent('pause');
        } else {
            this._triggerEvent('play');
        }
        return true;
    }
    return false;
}

Audio.prototype.addTextTrack = function() {
    notImplemented('audio.addTextTrack');
}

Audio.prototype.canPlayType = function() {
    notImplemented('audio.canPlayType');
}

Audio.prototype.load = function() {
    // TODO: We've already start loading if the src has changed. Not sure
    // what we should do here.
    //this._doSrcChange();
}

Audio.prototype.play = function() {
    if (this._setPaused(false)) {
        app.audioResume();
    }
};

Audio.prototype.pause = function() {
    if (this._setPaused(true)) {
        app.audioPause();
    }
};

Audio.prototype.setAttribute = function(attr, value) {
	this[attr] = value;
	console.log('attr: ' + attr);
}

Audio.prototype.addEventListener = function(ev, handler, ignored_bubbling) {
    if (!this._listeners[ev]) {
        this._listeners[ev] = []
    }
    this._listeners[ev].push(handler)
}

Audio.prototype.removeEventListener = function(ev, handler, ignored_bubbling) {
    var handlers = this._listeners[ev];
    if (handlers) {
        for (var i=0; i<handlers.length; i++) {
            if (handlers[i] == handler) {
                handlers.splice(i, 1);
                break;
            }
        }
    }
}

Audio.prototype.setNextSrc = function(src, metadata, optional_enqueue) {
    this._src = src;
    this._doSrcChange(metadata, optional_enqueue);
}

Audio.prototype.setStation = function(name, imageUrl, presetInfo) {
    name = name || '';
    imageUrl = imageUrl || '';
    var presetMethod = "play_preset";
    if (!presetInfo) {
        presetMethod = "";
    }
    presetInfo = presetInfo || {};

    this._station = {
        'id': name,
        'name': name,
        'imageUrl': imageUrl,
        'presetMethod': 'msg_socket_message_received',
        'presetInfo': {
            'namespace': 'urn:x-cast:com.google.cast.media',
            'sender_id': 0,
            'message': presetInfo
        }
    };
}

var instance = null;
var getAudioInstance = (function () {
    // Instance stores a reference to the Singleton
    return function() {
        if (!instance) {
            instance = new Audio();
        }
        return instance;
    };
})();

exports.getAudioInstance = getAudioInstance;

exports.setApp = setApp;
exports.setHttpFileStreamFactory = function(_httpFileStreamFactory) {
    httpFileStreamFactory = _httpFileStreamFactory;
}
exports.reset = function() {
    httpFileStreamFactory = HttpFileStreamFactory;
    instance = null;
    setApp(null);
}
