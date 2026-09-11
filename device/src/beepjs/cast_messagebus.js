///// CastMessageBus
//
// TODO: implement onClose
var util = require('util');

var cast_channel = require('cast_channel');

function Event(type, senderId, data) {
    this.type = type;
    this.senderId = senderId;
    this.data = data;
}

function CastMessageBus(messageType, namespace, app, manager) {
    this._messageType = messageType;
    this._namespace = namespace;
    this._app = app;
    this._manager = manager;

    this._channels = {};
}

CastMessageBus.prototype._handleMessage = function(senderId, message) {
    var m = this.deserializeMessage(message);
    // TODO: what order does chromecast do these in?
    if (this._channels[senderId]) {
        this._channels[senderId]._handleMessage(m);
    }
    if (this.onMessage) {
        this.onMessage(new Event('message', senderId, m));
    }
}

CastMessageBus.prototype.deserializeMessage = function(message) {
    return message;
}

CastMessageBus.prototype.onMessage = null;

CastMessageBus.prototype.serializeMessage = function(message) {
    return message;
}

CastMessageBus.prototype.getNamespace = function() {
    return this._namespace;
}

CastMessageBus.prototype.getMessageType = function() {
    return this._messageType;
}

CastMessageBus.prototype.send = function(senderId, message) {
    //console.log('Sending message to senderId (' + senderId + ') : '
    //        + JSON.stringify(message));
    if (this._manager.getSenders().indexOf(senderId) !== -1) {
        //console.log('Sender id: ' + senderId + ' found, sending message');
        // Do on a new run of main loop to avoid ubus recursion.
        var self = this;
        setTimeout(function() {
            self._app.msgSockSendMsg(
                    senderId, self._namespace, self.serializeMessage(message));
        }, 0);
    } else {
        // TODO: throw error?
        console.log('Sender id: ' + senderId + ' not found.');
    }
}

CastMessageBus.prototype.broadcast = function(message) {
    //console.log('Broadcasting message: ' + JSON.stringify(message));
    var senderIds = this._manager.getSenders();
    for (var i=0; i < senderIds.length; i++) {
        this.send(senderIds[i], message);
    }
}

CastMessageBus.prototype.getCastChannel = function(senderId) {
    if (this._manager.getSenders().indexOf(senderId) === -1) {
        // TODO: throw Error
    } else {
        var channel = new cast_channel.CastChannel(
                this._namespace, senderId, this);
        this._channels[senderId] = channel;
        return channel;
    }
}

function CastMessageBusJSON(messageType, namespace, app, manager) {
    CastMessageBus.apply(this, arguments);
}
util.inherits(CastMessageBusJSON, CastMessageBus);

CastMessageBusJSON.prototype.serializeMessage = JSON.stringify;

CastMessageBusJSON.prototype.deserializeMessage = JSON.parse;

exports.CastMessageBus = CastMessageBus;

exports.Event = Event;

exports.MessageType = {
    STRING: 0,
    JSON: 1,
    CUSTOM: 2
}

exports.makeCastMessageBus = function(messageType, namespace, app, manager) {
    var ctor = CastMessageBus;
    if (messageType === exports.MessageType.JSON) {
        ctor = CastMessageBusJSON
    }
    return new ctor(messageType, namespace, app, manager);
}
