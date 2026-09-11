// TODO:
//   - implement sending state in status.
//   - need a timer to figure out buffering status.
//   - can't play webradio after this. Fix distributor bug!
//   - add other idleReasons
//   - sent messages aren't being printed. Need to check all the possible
//     states and idleReasons

///// CastChannel
var cast = null;

function Event(type, data, senderId) {
    this.type = type;
    this.data = data;
    this.senderId = senderId;
}

function checkMediaInformation(mediaInfoMessage) {
    if (!mediaInfoMessage.contentId || !mediaInfoMessage.streamType
            || !mediaInfoMessage.contentType) {
        console.log('ERROR: media information missing required field');
        return null;
    }
    return mediaInfoMessage;
}

function setCast(_cast) {
    cast = _cast;
}

function MediaManager(mediaElement) {
    this.setMediaElement(mediaElement)
    this._mediaSessionId = 0;
    this._idleReason = null;
    this._isBuffering = false;
    this._mediaStartTime = 0;
    this._currentTime = null;
    this._lastMessageTime = null;
    this._outstandingRequest = null;

    this._manager = cast.receiver.BeepReceiverManager.getInstance();
    this._bus = this._manager.getCastMessageBus(
            'urn:x-cast:com.google.cast.media',
            cast.receiver.CastMessageBus.MessageType.JSON);
    var self = this;
    this._bus.onMessage = function(message) {
        self._handleMessage(message);
    }
    setInterval(function() {
        self._timer()
    }, 2000);
}

// Called every 2s, unlike Chromecast which is every 1s. We only have second
// resolution on media.CurrentTime right now, so we need to use a longer
// period.
MediaManager.prototype._timer = function() {
    var state = this._getState();
    if (state != "IDLE" && state != "PAUSED") {
        var lastTime = this._currentTime;
        this._currentTime = this._mediaElement.currentTime;
        if (lastTime !== null) {
            var wasBuffering = this._isBuffering;
            this._isBuffering = this._currentTime === lastTime;
            if(wasBuffering !== null && wasBuffering !== this._isBuffering) {
                console.log("Buffering state changed, isBuffering: "
                        + this._isBuffering + " old time: " + lastTime
                        + " current time: " + this._currentTime);
                // broadcast status, don't include media information
                this.broadcastStatus(false)
            }
        }
        // TODO: we don't do the time drift check that CC does, may not be
        // needed.
    } else {
        this._currentTime = null;
        this._isBuffering = null;
    }
};

// This differs from Chromecast, because our audio element always autostarts
// and is in the playing state after an initial load. So we set _isBuffering
// in _handleLoad in order to start off in the buffering state.
MediaManager.prototype._getState = function () {
    if (!this._mediaInformation) {
        return "IDLE";
    }
    var mediaElement = this._mediaElement;
    if (mediaElement.paused) {
        if (mediaElement.duration // have a duration
            && mediaElement.currentTime != null  // and a currentTime
            && mediaElement.duration != mediaElement.currentTime) { // not at end
            return "PAUSED";
        } else {
            return "IDLE";
        }
    } else {
        return this._isBuffering ? "BUFFERING" : "PLAYING";
    }
}

MediaManager.prototype._handleMessage = function(ev) {
    var senderId = ev.senderId;
    var message = ev.data;

    // TODO: check mediaSessionId

    if (message.type == 'LOAD') {
        this._handleLoad(senderId, message);
    } else if (message.type == 'PAUSE') {
        this._handlePause(senderId, message);
    } else if (message.type == 'PLAY') {
        this._handlePlay(senderId, message);
    } else if (message.type == 'STOP') {
        this._handleStop(senderId, message);
    } else if (message.type == 'GET_STATUS') {
        this._handleGetStatus(senderId, message);
    } else if (message.type == 'VOLUME') {
        this._handleSetVolume(senderId, message);
    } else {
        console.log('ERROR: Invalid MediaManager message');
    }
}

MediaManager.prototype._handleLoad = function(senderId, message) {
    if (!message.requestId || !message.media
            || !checkMediaInformation(message.media)) {
        console.log('ERROR: invalid LOAD message');
        return;
    }

    if (this._loadInfo) {
        this.sendLoadError('LOAD_CANCELLED');
    } else if (this._mediaInformation) {
        this.resetMediaElement('INTERRUPTED');
    }

    this._loadInfo = {
        senderId: senderId,
        message: message
    }
    this._mediaStartTime = message.currentTime || 0;
    this._mediaInformation = message.media;
    this._mediaSessionId++;
    this._isBuffering = true;
    var ev = new Event('load', message, senderId);
    if (this.onLoad) {
        this.onLoad(ev);
    }
}

MediaManager.prototype._handlePause = function(senderId, message) {
    if (!message.mediaSessionId || !message.requestId) {
        console.log('ERROR: invalid PAUSE message');
        return;
    }
    this._outstandingRequest = message;

    var ev = new Event('pause', message, senderId);
    if (this.onPause) {
        this.onPause(ev);
    }
}

MediaManager.prototype._handlePlay = function(senderId, message) {
    if (!message.mediaSessionId || !message.requestId) {
        console.log('ERROR: invalid PLAY message');
        return;
    }
    this._outstandingRequest = message;

    var ev = new Event('play', message, senderId);
    if (this.onPlay) {
        this.onPlay(ev);
    }
}

MediaManager.prototype._handleStop = function(senderId, message) {
    if (!message.mediaSessionId || !message.requestId) {
        console.log('ERROR: invalid STOP message');
        return;
    }

    var ev = new Event('stop', message, senderId);
    if (this.onStop) {
        this.onStop(ev);
    }
}

MediaManager.prototype._handleGetStatus = function(senderId, message) {
    if (!message.requestId) {
        console.log('ERROR: invalid GET_STATUS message');
        return;
    }

    // TODO: mediaSessionId is optional, if none is provided "status for all
    // media session IDs will be returned"

    var ev = new Event('getstatus', message, senderId);
    if (this.onGetStatus) {
        this.onGetStatus(ev);
    }
}

MediaManager.prototype._handleSetVolume = function(senderId, message) {
    console.log(JSON.stringify(message));
    if (!message.mediaSessionId || !message.requestId || !message.volume
            || !(message.volume.level || message.volume.muted)) {
        console.log('ERROR: invalid VOLUME message');
        return;
    }

    this._outstandingRequest = message;
    // TODO: where to validate volume level?

    var ev = new Event('setvolume', message, senderId);
    if (this.onSetVolume) {
        this.onSetVolume(ev);
    }
}

MediaManager.prototype._makeMediaStatus = function(includeMedia, opt_customData) {
    var mediaStatus = {
        mediaSessionId: this._mediaSessionId,
        playbackRate: this._mediaElement.playbackRate,
        playerState: this._getState(),
        currentTime: this._mediaElement.currentTime,
        supportedMediaCommands: 1,  // TODO
        volume: this._mediaElement.volume
    }
    if (includeMedia) {
        mediaStatus.media = this._mediaInformation || this._prevMediaInformation || undefined;
    }
    if (!this._mediaInformation) {
        this._prevMediaInformation = null;
    }
    if (mediaStatus.playerState == 'IDLE') {
        mediaStatus.idleReason = this._idleReason ? this._idleReason : null;
    }
    if (opt_customData) {
        mediaStatus.customData = opt_customData;
    }

    // TODO: Should only call this if not null ala chromecast.
    return this.customizedStatusCallback(mediaStatus);
}

MediaManager.prototype._makeMediaStatusResponse = function(
        includeMedia, opt_requestId, opt_customData) {
    var mediaStatus = this._makeMediaStatus(includeMedia, opt_customData);
    if (mediaStatus === null) {
        return null;
    }
    var response = {
        requestId: opt_requestId || 0,
        type: 'MEDIA_STATUS',
        status: [mediaStatus]
    }
    if (opt_customData) {
        response.customData = opt_customData;
    }
    return response;
}

MediaManager.prototype._updateTimes = function() {
    this._currentTime = this._mediaElement.currentTime;
    this._lastMessageTime = this._mediaElement.currentTime;
}

MediaManager.prototype.customizedStatusCallback = function(mediaStatus) {
    return mediaStatus;
}

MediaManager.prototype.onEnded = function(obj) {
    console.log('Media file finished playing');
}

MediaManager.prototype.onGetStatus = function(ev) {
    this.sendStatus(ev.senderId, ev.data.requestId, true, ev.data.customData);
}

MediaManager.prototype.onError = function(obj) {
    console.log('Got media element error when not loading');
    this.resetMediaElement('ERROR');
}

MediaManager.prototype.onLoad = function(ev) {
    var message = ev.data;

    //this._mediaElement.autoplay = false;
    var metadata = null;
    if (this._mediaInformation.metadata
            && this._mediaInformation.metadata.metadataType === 3) {
        metadata = this._mediaInformation.metadata;
    }
    this._mediaElement.setNextSrc(message.media.contentId, metadata);
    //this._mediaElement.autoplay = message.autoplay;

    // TODO: calling this like Chromecast does doesn't work, because we've
    // just acquired the audio. But it looks like Chrome starts loading
    // as soon as we set .src, so what's the point of this?
    //this._mediaElement.load();
}

MediaManager.prototype.onLoadMetadataError = function(loadInfo) {
    console.log('metadata load error');
    this.resetMediaElement('ERROR', false);
    this.sendLoadError('LOAD_FAILED');
}

MediaManager.prototype.onMetadataLoaded = function(loadInfo) {
    // TODO: do seek here if it it's in loadInfo.message.currentTime
    console.warn('LOAD with currentTime not yet supported');
    this.sendLoadComplete();
}

MediaManager.prototype.onPause = function(ev) {
    this._mediaElement.pause();
}

MediaManager.prototype.onPlay = function(ev) {
    this._mediaElement.play();
}

MediaManager.prototype.onSeek = function(ev) {
    console.warn('MediaManager onSeek not implemented');
}

MediaManager.prototype.onSetVolume = function(ev) {
    var message = ev.data;
    if (message.volume.level) {
        this._mediaElement.volume = message.volume.level;
    }
    if (message.volume.muted) {
        this._mediaElement.muted = message.volume.muted;
    }
}

MediaManager.prototype.onStop = function(ev) {
    this._mediaElement.src = null;
    // As above we don't really need to do this twice.
    //this._mediaElement.load();
}

MediaManager.prototype.getMediaInformation = function() {
    return this._mediaInformation;
}

MediaManager.prototype.setMediaInformation = function(
        mediaInformation, opt_broadcast, opt_broadcastStatusCustomData) {
    this._mediaInformation = mediaInformation;
    if (opt_broadcast) {
        this.broadcastStatus(true, 0, opt_broadcastStatusCustomData);
    }
}

MediaManager.prototype.broadcastStatus = function(
        includeMedia, opt_requestId, opt_customData) {
    var mediaStatusResponse = this._makeMediaStatusResponse(
            includeMedia, opt_requestId, opt_customData);
    // user can override customizedStatusCallback to cause a null response
    // to be returned, in which case we don't send the response.
    if (mediaStatusResponse != null) {
        this._bus.broadcast(mediaStatusResponse);
        this._updateTimes();
    }
}

MediaManager.prototype.setIdleReason = function(idleReason) {
    this._idleReason = idleReason;
}

MediaManager.prototype.sendError = function(
        senderId, requestId, type, opt_reason, opt_customData) {
    console.log('Sending error message to: ' + senderId);
    var response = {
        requestId: requestId,
        type: type
    };
    if (opt_reason) {
        response.reason = opt_reason;
    }
    if (opt_customData) {
        response.customData = opt_customData;
    }
    this._bus.send(senderId, response);
}

MediaManager.prototype.sendStatus = function(
        senderId, requestId, includeMedia, opt_customData) {
    var mediaStatusResponse = this._makeMediaStatusResponse(
            includeMedia, requestId, opt_customData);
    // user can override customizedStatusCallback to cause a null response
    // to be returned, in which case we don't send the response.
    // TODO: Chromecast sets the request id here rather than passing it
    //     into makeMediaStatusResponse. Check if this is ok.
    if (mediaStatusResponse != null) {
        this._bus.send(senderId, mediaStatusResponse);
        this._updateTimes();
    }
}

MediaManager.prototype.setMediaElement = function(mediaElement) {
    this._mediaElement = mediaElement;
    var self = this;

    // TODO: clear callbacks
    this._mediaElement.addEventListener('loadedmetadata', function() {
        console.log('Loaded metadata');
        if (self._loadInfo) {
            // TODO: set duration from audio element once we have it
            // self._mediaInformation.duration = self._mediaElement.duration
            self._isBuffering = true;
            if (self.onMetadataLoaded) {
                self.onMetadataLoaded(self._loadInfo);
            } else {
                self._loadInfo = null;
            }
        }
    });

    this._mediaElement.addEventListener('error', function() {
        console.log('Metadata load error');
        if (self._loadInfo) {
            if (self.onLoadMetadataError) {
                self.onLoadMetadataError(self._loadInfo);
            }
            else {
                self._loadInfo = null;
            }
        }
        else if (self.onError) {
            // Call with empty object because CC docs says it takes an object
            // but it looks like that object isn't defined.
            self.onError({});
        }
    });

    this._mediaElement.addEventListener('ended', function() {
        if (self.onEnded) {
            self.onEnded();
        }
    });

    this._mediaElement.addEventListener('pause', function() {
        self.broadcastStatus(false,
            self._outstandingRequest ? self._outstandingRequest.requestId
            : undefined);
        self._outstandingRequest = null;
    });

    this._mediaElement.addEventListener('play', function() {
        // Don't send if this is the result of a LOAD, which triggers
        // the mediaElement's paused state to change to false.
        if (!this._loadInfo) {
            self.broadcastStatus(false,
                self._outstandingRequest ? self._outstandingRequest.requestId
                : undefined);
            self._outstandingRequest = null;
        }
    });

    this._mediaElement.addEventListener('volumechange', function() {
        self.broadcastStatus(false,
            self._outstandingRequest ? self._outstandingRequest.requestId
            : undefined);
        self._outstandingRequest = null;
    });
}

MediaManager.prototype.sendLoadError = function(opt_errorType, opt_customData) {
    if (this._loadInfo) {
        this.sendError(
                this._loadInfo.senderId, this._loadInfo.message.requestId,
                opt_errorType || 'LOAD_FAILED', null, opt_customData);
        this._loadInfo = null;
    } else {
        console.warn('no current load request, so not sending load error');
    }
}

MediaManager.prototype.sendLoadComplete = function(opt_customData) {
    if (this._loadInfo) {
        this.broadcastStatus(
                true, this._loadInfo.message.requestId, opt_customData);
        this._loadInfo = null;
    } else {
        console.warn('no current load request, so not sending load complete');
    }
}

// TODO: Go through and find all the places where this is called (this.H)
// and make sure they're implemented.
MediaManager.prototype.resetMediaElement = function(
        opt_idleReason, opt_broadcast, opt_requestId,
        opt_broadcastStatusCustomData) {
    if (opt_broadcast === null) {
        opt_broadcast = true;
    }
    if (this._mediaInformation) {
        console.log("Resetting media element")
        // TODO: our audio object doesn't have a removeAttribute method.
        this._mediaElement.src = undefined;

        this._mediaStartTime = 0;

        // TODO: This is still a NOOP for us
        this._mediaElement.load()

        if (opt_idleReason) {
            this._idleReason = opt_idleReason;
        }
        // TODO: This is use in "ic = function" which I believe generates the
        // media status. So look at that function and make sure ours does
        // the same thing.
        this._prevMediaInformation = this._mediaInformation;
        this._mediaInformation = null;
        if (opt_broadcast) {
            this.broadcastStatus(
                    false, opt_requestId, opt_broadcastStatusCustomData);
        }
    } else {
        console.log('Can\'t reset media element, don\'t have media.');
    }
}

exports.setCast = setCast;
exports.MediaManager = MediaManager;
