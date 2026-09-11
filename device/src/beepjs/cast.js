var audio = require('audio');
var beep = require('beep');
var cast_messagebus = require('cast_messagebus');
var cast_mediamanager = require('cast_mediamanager');

var beepAppFactory = beep.BeepApp;

function Event(type, data) {
    this.type = type;
    this.data = data;
}

function Sender(id, userAgent) {
    this.id = id;
    this.userAgent = userAgent;
}

function BeepReceiverManager() {
    this._ready = false;
    this._app = null;

    // These are maintained together, the indices always line up.
    this._senderIds = [];
    this._senders = [];

    this._messageBusses = {};
}


///// private

function handleUbusSenderConnected(req) {
    // TODO: idle sender tracking
    // TODO: we need to check if we already have this sender.

    var sender = new Sender(req.sender_id, req.user_agent);
    this._senderIds.push(req.sender_id);
    this._senders.push(sender);

    if (this.onSenderConnected) {
        this.onSenderConnected(new Event('senderconnected', sender));
    }
}

function handleUbusSenderDisconnected(req) {
    var index = this._senderIds.indexOf(req.sender_id);
    if (index !== -1) {
        var sender = this._senders[index];
        this._senderIds.splice(index, 1);
        this._senders.splice(index, 1);

        if (this.onSenderDisconnected) {
            this.onSenderDisconnected(new Event('senderdisconnected', sender));
        }
    }
}

function handleUbusMessageReceived(req) {
    if (this._messageBusses[req.namespace]) {
        this._messageBusses[req.namespace]._handleMessage(
                req.sender_id, req.message);
    }
}


///// public

BeepReceiverManager.prototype.start = function(config) {
    if (!config) {
        config = new Config();
    }
    this.config = config;

    if (!this.config.appName) {
        console.log('ERROR: BeepReceiverManager must be initialized with '
                + 'an appConfig containing .appName');
        process.exit(1);
    }

    this._app = beepAppFactory('beep.app.' + this.config.appName);

    var self = this;
    // all messaging is asyncrhonous, we defer processing of incoming messages
    // to avoid dead-locking beepcomm.
    this._app._onUbus_msg_socket_sender_connected = function(req) {
        setTimeout(function() {
            handleUbusSenderConnected.call(self, req);
        }, 0);
        return beep.ubus_success(null);
    };
    this._app._onUbus_msg_socket_sender_disconnected = function(req) {
        setTimeout(function() {
            handleUbusSenderDisconnected.call(self, req);
        }, 0);
        return beep.ubus_success(null);
    };
    this._app._onUbus_msg_socket_message_received = function(req) {
        setTimeout(function() {
            handleUbusMessageReceived.call(self, req);
        }, 0);
        return beep.ubus_success(null);
    };
    this._app._onUbus_play_preset = function(req) {
        // TODO: we run this in the current pass of the main loop because we
        // need to respond with the user's response. Don't do this.
        var result = null;
        if (self.onPlayPreset) {
            result = self.onPlayPreset(new Event('playpreset', req));
        }
        return beep.ubus_success(result);
    }
    this._app._onUbus_skip = function(req) {
        // TODO: we run this in the current pass of the main loop because we
        // need to respond with the user's response. Don't do this.
        var result = null;
        if (self.onSkipRequested) {
            result = self.onSkipRequested(new Event('skiprequested', req));
        }
        return beep.ubus_success(result);
    }
    this._app._onUbus_resume = function(req) {
        if (self.onPlayRequested) {
            setTimeout(function() {
                self.onPlayRequested(new Event('playrequested'));
            }, 0);
        }
        return beep.ubus_success(true);
    }
    this._app._onUbus_pause = function(req) {
        if (self.onPauseRequested) {
            setTimeout(function() {
                self.onPauseRequested(new Event('pauserequested'));
            }, 0);
        }
        return beep.ubus_success(true);
    }
    audio.setApp(this._app);

    this._app.startApp();

    var audioElement = audio.getAudioInstance();
    audioElement.specialVolumeChangeHandler = function() {
        if (self.onSystemVolumeChanged) {
            self.onSystemVolumeChanged(
                new Event('systemvolumechanged', audioElement.volume));
        }
    };

    this._ready = true;
    if (this.onReady) {
        // TODO: What data does the ready event have?
        this.onReady(new Event('ready', null));
    }
    console.log('started: ' + this.config.appName);
};

BeepReceiverManager.prototype.isSystemReady = function() {
    return this._ready;
};

BeepReceiverManager.prototype.getSenders = function() {
    // Return a copy so the user can't mess with ours.
    return this._senderIds.slice(0);
}

BeepReceiverManager.prototype.getSender = function(senderId) {
    var index = this._senderIds.indexOf(senderId);
    if (index === -1) {
        return null;
    } else {
        var sender = this._senders[index];
        return new Sender(sender.id, sender.userAgent);
    }
}

BeepReceiverManager.prototype.getApplicationData = function() {
    // TODO: put the application data in as specified by Chromecast.
    if (this._ready) {
        return {};
    } else {
        return null;
    }
}

BeepReceiverManager.prototype.setApplicationState = function(statusText) {
    // TODO: What is this for? Do something with it.
}

BeepReceiverManager.prototype.getCastMessageBus = function(
        namespace, opt_messageType) {
    var bus =  cast_messagebus.makeCastMessageBus(
            opt_messageType, namespace, this._app, this);
    this._messageBusses[namespace] = bus;
    return bus;
}

// Takes a station name, an image for the station, and a user specific object
// that will be returned when the user requests this station to play again.
//
// Pass in a null name to indicate that there is no station name and no preset
// available.
//
// Pass in a null presetInfo to indicate that this station may not be saved
// as a preset.
BeepReceiverManager.prototype.setStation = function(name, imageUrl, presetInfo) {
    var audioElement = audio.getAudioInstance();
    audioElement.setStation(name, imageUrl, presetInfo);
}

BeepReceiverManager.prototype.onPlayRequested = function(event) {
    var audioElement = audio.getAudioInstance();
    audioElement.play();
}

BeepReceiverManager.prototype.onPauseRequested = function(event) {
    var audioElement = audio.getAudioInstance();
    audioElement.pause();
}

function Config() {
    this.appName = "";
}


var receiver = {}
var manager = null;
receiver.BeepReceiverManager = {
    getInstance: function() {
        if (!manager) {
            manager = new BeepReceiverManager();
        }
        return manager;
    },
    Config: Config,
    Event: Event
}
receiver.CastMessageBus = {
    Event: cast_messagebus.Event,
    MessageType: cast_messagebus.MessageType
}
receiver.MediaManager = cast_mediamanager.MediaManager;
receiver.system = {
    // TODO: Add ApplicationData
    Sender: Sender
}

cast_mediamanager.setCast({
    receiver: receiver
});

exports.receiver = receiver;
exports.getAudioInstance = audio.getAudioInstance

// Used for unit testing.
exports.setBeepAppFactory = function(_beepAppFactory) {
    beepAppFactory = _beepAppFactory;
}
exports.setAudioHttpFileStreamFactory = audio.setHttpFileStreamFactory;

exports.reset = function() {
    beepAppFactory = beep.BeepApp;
    manager = null;
    audio.reset();
}
