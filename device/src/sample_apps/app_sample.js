var cast = require('cast');
var XMLHttpRequest = require('XMLHttpRequest').XMLHttpRequest;

var url = require('url');

var xmlhttp = new XMLHttpRequest();
xmlhttp.onreadystatechange = function() {
    if (xmlhttp.readyState == XMLHttpRequest.DONE && xmlhttp.status == 200) {
        console.log('XMLHttp response: ' + xmlhttp.responseText.length);
    }
}

xmlhttp.open("GET", "http://www.google.com", true);
xmlhttp.send();

var appConfig = new cast.receiver.BeepReceiverManager.Config();
appConfig.appName = 'sample';

var manager = cast.receiver.BeepReceiverManager.getInstance();

manager.onSenderConnected = function(ev) {
    console.log('sender connected: ' + JSON.stringify(ev));
    console.log('senders ' + JSON.stringify(manager.getSenders()));
}

manager.onSenderDisconnected = function(ev) {
    console.log('sender connected: ' + JSON.stringify(ev));
    console.log('senders ' + JSON.stringify(manager.getSenders()));
}

manager.onReady = function(ev) {
    console.log('BeepReceiverManager is ready. ' + JSON.stringify(ev));
}

var messageBus = manager.getCastMessageBus('ns');

messageBus.onMessage = function(message) {
    console.log('Received a message on bus(' + messageBus.getNamespace()
                + ') : ' + JSON.stringify(message));
}

manager.start(appConfig);

var audio = cast.getAudioInstance();

audio.addEventListener('progress', function() {
    console.log('Got progress event. currentTime: ' + this.currentTime);
});

var mediaManager = new cast.receiver.MediaManager(audio);

mediaManager.origOnLoad = mediaManager.onLoad;
mediaManager.onLoad = function(event) {
    console.log('RUNNING ONLOAD');
    mediaManager.origOnLoad(event);
    console.log('DONE');

    manager.setStation("sample station");
}

mediaManager.getMediaInformation();
