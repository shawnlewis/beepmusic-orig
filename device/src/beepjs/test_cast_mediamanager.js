var assert = require('assert');
var util = require('util');
var EventEmitter = require('events').EventEmitter;

var cast = require('cast');


///// Mocks

function BeepAppMock() {
    this.responses = [];
    this.calls = [];
    this._audioEventHandler = null;
}

BeepAppMock.prototype.startApp = function() {
    console.log('app.startApp called');
};
BeepAppMock.prototype.subscribe = function(target, handler) {
    console.log('app.subscribe called for target: ' + target);
    if (target == 'beep.state.distributor._local_') {
        this._audioEventHandler = handler;
    }
};

BeepAppMock.prototype.audioAcquire = function() {
    this.calls.push('acquire');
};
BeepAppMock.prototype.audioResume = function() {
    this.calls.push('resume');
};
BeepAppMock.prototype.audioPause = function() {
    this.calls.push('pause');
};

BeepAppMock.prototype.msgSockSendMsg = function(
        senderId, namespace, message) {
    this.responses.push({
        senderId: senderId,
        namespace: namespace,
        message: JSON.parse(message)
    });
}

BeepAppMock.prototype.injectSenderConnected = function(senderId, userAgent) {
    this._onUbus_msg_socket_sender_connected({
        sender_id: senderId,
        user_agent: userAgent
    });
}

BeepAppMock.prototype.injectMessageReceived = function(
        senderId, namespace, message) {
    this._onUbus_msg_socket_message_received({
        sender_id: senderId,
        namespace: namespace,
        message: message
    });
}

BeepAppMock.prototype.injectAudioEvent = function(message) {
    if (!this._audioEventHandler) {
        console.error('Trying to injectAudioEvent but subscribe was never called for beep.state.distributor._local_');
        process.exit(1);
    }
    this._audioEventHandler(
            'beep.state.distributor._local_', JSON.stringify(message));
}

BeepAppMock.prototype.sendMediaRequest = function(senderId, request) {
    this.injectMessageReceived(
            senderId,
            'urn:x-cast:com.google.cast.media',
            JSON.stringify(request));
}

BeepAppMock.prototype.clearResponses = function() {
    this.responses = [];
}

BeepAppMock.prototype.clearCalls = function() {
    this.calls = [];
}


function HttpFileStreamMock(app, url) {
}
util.inherits(HttpFileStreamMock, EventEmitter);

HttpFileStreamMock.prototype.start = function() {
    console.log('stream.start called');
};


function TimersMock() {
    this._time = 0;
    this._timers = [];
    var self = this;
    globals.setInterval = function(func, period) {
        self._timers.push({
            func: func,
            length: period,
            left: period,
            repeat: true
        });
    };
    globals.setTimeout = function(func, period) {
        self._timers.push({
            func: func,
            length: period,
            left: period,
            repeat: false
        });
    };
}

TimersMock.prototype._runOneTimerWithinTime = function(delta) {
    // Find the timer closest to firing.
    var firstTimerIndex = null;
    var firstTimerTime = 1024 * 1024 * 1024;
    for (var i=0; i<this._timers.length; i++) {
        if (this._timers[i].left < firstTimerTime) {
            firstTimerTime = this._timers[i].left;
            firstTimerIndex = i;
        }
    }
    var usedTime = null;
    if (firstTimerIndex !== null && firstTimerTime <= delta) {
        var timer = this._timers[firstTimerIndex];
        usedTime = timer.left;
        timer.func();
        if (timer.repeat) {
            timer.left = timer.length + usedTime;
        } else {
            this._timers.splice(firstTimerIndex, 1);
        }
    }
    return usedTime;
}

TimersMock.prototype._subtractTime = function(delta) {
    for (var i=0; i<this._timers.length; i++) {
        this._timers[i].left -= delta;
    }
}

TimersMock.prototype.advance = function(delta) {
    while (usedTime = this._runOneTimerWithinTime(delta)) {
        this._subtractTime(usedTime);
        delta -= usedTime;
    }
    this._subtractTime(delta);
}

TimersMock.prototype.dump = function(delta) {
    console.log('### Begin timer dump ###');
    for (var i=0; i < this._timers.length; i++) {
        console.log('Timer #' + i + ': ' + JSON.stringify(this._timers[i]));
    }
    console.log('### End timer dump ###');
}


function CastTest() {
    cast.reset();

    ///// Setup and inject mocks

    this.beepApp = new BeepAppMock();

    var self = this;
    function BeepAppMockFactory() {
        return self.beepApp;
    }
    cast.setBeepAppFactory(BeepAppMockFactory);

    function HttpFileStreamFactory(app, url) {
        return new HttpFileStreamMock(app, url);
    }
    cast.setAudioHttpFileStreamFactory(HttpFileStreamFactory);

    this.time = new TimersMock();

    ///// Initialize cast

    var appConfig = new cast.receiver.BeepReceiverManager.Config();
    appConfig.appName = 'test_cast';
    this.manager = cast.receiver.BeepReceiverManager.getInstance();
    this.manager.start(appConfig);

    this.audio = cast.getAudioInstance();
}

function test(test_func) {
    var obj = new CastTest();
    test_func.call(obj);
}

test(function() {
    this.beepApp.injectSenderConnected(1, 'user a');
    this.beepApp.injectSenderConnected(2, 'user b');
    this.beepApp.injectSenderConnected(3, 'user c');

    mediaManager = new cast.receiver.MediaManager(this.audio);

    assert.equal(0, this.audio.currentTime);
    assert.equal(true, this.audio.paused);

    // Check that initial playerState is IDLE
    assert.equal(0, this.beepApp.responses.length);
    this.beepApp.sendMediaRequest(1, {
        type: 'GET_STATUS',
        requestId: 1,
    });
    assert.equal(1, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('MEDIA_STATUS', message.type);
    assert.equal('IDLE', message.status[0].playerState);
    this.beepApp.clearResponses();

    // Check that a LOAD request broadcasts a message with media in the
    // BUFFERING state.
    this.beepApp.sendMediaRequest(1, {
        type: 'LOAD',
        requestId: 2,
        media: {
            contentId: 'http://example.com//music.mp3',
            streamType: 'NONE',
            contentType: 'audio/mpeg'
        },
    });
    assert.deepEqual(['acquire', 'resume'], this.beepApp.calls);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('MEDIA_STATUS', message.type);
    assert.equal('BUFFERING', message.status[0].playerState);
    assert.equal(0, this.audio.currentTime);
    assert.equal(false, this.audio.paused);
    this.beepApp.clearCalls();
    this.beepApp.clearResponses();

    // Audio should not change if time advances.
    this.time.advance(1);
    assert.equal(0, this.audio.currentTime);
    assert.equal(false, this.audio.paused);

    // Distributor sends an audio_state_change with audio_state 'working',
    // audio should not change.
    this.beepApp.injectAudioEvent({
        'event_type': 'audio_state_change',
        'state': {
            'audio_state': 'working',
            'track_elapsed_secs': 0
        }
    });
    assert.equal(0, this.audio.currentTime);
    assert.equal(false, this.audio.paused);

    // Distributor sends progress event with track_elapsed_secs 0, nothing
    // should change.
    this.time.advance(1);
    this.beepApp.injectAudioEvent({
        'event_type': 'progress',
        'state': {
            'audio_state': 'working',
            'track_elapsed_secs': 0
        }
    });
    assert.equal(0, this.audio.currentTime);
    assert.equal(false, this.audio.paused);

    // Check that we get no updates as time advances.
    this.time.advance(8000);
    assert.equal(0, this.beepApp.responses.length);
    assert.equal(0, this.audio.currentTime);
    assert.equal(false, this.audio.paused);

    // audio_state -> playing
    this.beepApp.injectAudioEvent({
        'event_type': 'audio_state_change',
        'state': {
            'audio_state': 'playing',
            'track_elapsed_secs': 0
        }
    });
    this.time.advance(1);
    assert.equal(false, this.audio.paused);

    // Now track_elapsed_secs begins to advance
    this.beepApp.injectAudioEvent({
        'event_type': 'progress',
        'state': {
            'audio_state': 'working',
            'track_elapsed_secs': 1
        }
    });
    this.time.advance(2000);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('PLAYING', message.status[0].playerState);
    this.beepApp.clearResponses();

    // If track_elapsed_secs doesn't advance we go back to BUFFERING.
    this.time.advance(2000);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('MEDIA_STATUS', message.type);
    assert.equal('BUFFERING', message.status[0].playerState);
    assert.equal(false, this.audio.paused);
    this.beepApp.clearResponses();

    // Advanced track_elapsed_secs again, should go back to playing
    this.beepApp.injectAudioEvent({
        'event_type': 'progress',
        'state': {
            'audio_state': 'working',
            'track_elapsed_secs': 2
        }
    });
    this.time.advance(2000);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('PLAYING', message.status[0].playerState);
    this.beepApp.clearResponses();

    // Now PAUSE, we should get a broadcast with playerState PAUSED immediately
    this.beepApp.sendMediaRequest(1, {
        mediaSessionId: 1,
        type: 'PAUSE',
        requestId: 3
    });
    assert.deepEqual(['pause'], this.beepApp.calls);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('PAUSED', message.status[0].playerState);
    this.beepApp.clearResponses();

    // Distributor sends a state update, we shouldn't get another PAUSED
    // announcement.
    this.time.advance(1);
    this.beepApp.injectAudioEvent({
        'event_type': 'audio_state_change',
        'state': {
            'audio_state': 'paused',
            'track_elapsed_secs': 2
        }
    });
    assert.equal(0, this.beepApp.responses.length);

    this.time.advance(8000);
    assert.equal(0, this.beepApp.responses.length);

    // Another PAUSE should not generate a broadcast.
    this.beepApp.sendMediaRequest(1, {
        mediaSessionId: 1,
        type: 'PAUSE',
        requestId: 4
    });
    assert.deepEqual(['pause'], this.beepApp.calls);
    assert.equal(0, this.beepApp.responses.length);
    this.beepApp.clearCalls();

    this.time.advance(2000);

    // A PLAY should broadcast playerState PLAYING
    this.beepApp.sendMediaRequest(1, {
        mediaSessionId: 1,
        type: 'PLAY',
        requestId: 5
    });
    assert.deepEqual(['resume'], this.beepApp.calls);
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('PLAYING', message.status[0].playerState);
    this.beepApp.clearResponses();

    // Distributor sends a state update, we shouldn't get another PLAYING
    // announcement.
    this.time.advance(1);
    this.beepApp.injectAudioEvent({
        'event_type': 'audio_state_change',
        'state': {
            'audio_state': 'playing',
            'track_elapsed_secs': 2
        }
    });
    assert.equal(0, this.beepApp.responses.length);

    this.beepApp.injectAudioEvent({
        'event_type': 'progress',
        'state': {
            'audio_state': 'playing',
            'track_elapsed_secs': 3
        }
    });
    this.time.advance(2000);
    assert.equal(0, this.beepApp.responses.length);

    // A pause from something else controlling distributor should cause
    // us to broadcast a PAUSED playerState.
    this.beepApp.injectAudioEvent({
        'event_type': 'audio_state_change',
        'state': {
            'audio_state': 'paused',
            'track_elapsed_secs': 3
        }
    });
    assert.equal(3, this.beepApp.responses.length);
    var message = this.beepApp.responses[0].message;
    assert.equal('PAUSED', message.status[0].playerState);
    this.beepApp.clearResponses();
});
