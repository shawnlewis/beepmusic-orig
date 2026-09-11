var cast = require('cast');

var appConfig = new cast.receiver.BeepReceiverManager.Config();
appConfig.appName = 'echo';

var manager = cast.receiver.BeepReceiverManager.getInstance();
manager.start(appConfig);

var messageBus = manager.getCastMessageBus('echo');

manager.onSenderConnected = function(ev) {
    console.log('sender connected: ' + JSON.stringify(ev));
    console.log('senders ' + JSON.stringify(manager.getSenders()));
    messageBus.broadcast('sender connected');
}

manager.onSenderDisconnected = function(ev) {
    console.log('sender connected: ' + JSON.stringify(ev));
    console.log('senders ' + JSON.stringify(manager.getSenders()));
    messageBus.broadcast('sender disconnected');
}

manager.onReady = function(ev) {
    console.log('BeepReceiverManager is ready. ' + JSON.stringify(ev));
}

messageBus.onMessage = function(message) {
    console.log('Received a message on bus(' + messageBus.getNamespace()
                + ') : ' + JSON.stringify(message));
    setTimeout(function() {
        messageBus.broadcast(message.data);
    }, Math.floor(Math.random() * 3000));
}
