///// CastChannel

function Event(type, message) {
    this.type = type;
    this.message = message;
}

function CastChannel(namespace, senderId, bus) {
    this._namespace = namespace;
    this._senderId = senderId;
    this._bus = bus;
}

CastChannel.prototype._handleMessage = function(senderId, message) {
    if (this.onMessage) {
        this.onMessage(new Event('message', message));
    }
}

CastChannel.prototype.onMessage = null;

CastChannel.prototype.getNamespace = function() {
    return this._namespace;
}

CastChannel.prototype.getSenderId = function() {
    return this._senderId;
}

CastChannel.prototype.send = function(message) {
    this._bus.send(this._senderId, message);
}

exports.CastChannel = CastChannel;
exports.Event = Event;
