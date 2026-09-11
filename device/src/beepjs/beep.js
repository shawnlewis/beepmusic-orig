var util = require('util');
var beep = process.binding('beep');

function BeepApp(name) {
    if (!(this instanceof BeepApp)) {
        return new BeepApp(name);
    }
    this.nativeBeepApp = beep.BeepApp(name);
    Object.keys(beep.BeepApp.prototype).forEach(function(key) {
        if (typeof(this.nativeBeepApp[key]) == 'function') {
            this[key] = this[key].bind(this.nativeBeepApp);
        } else {
            this.__defineGetter__(key, function() {
                return this.nativeBeepApp[key];
            });
        }
    }, this);

    var self = this;
    this.nativeBeepApp._onUbusMethod = function(method_name, req_str) {
        console.log('ubus method: ' + method_name + ' called');
        // This contains private user data, don't log in production
        //console.log('req: ' + req_str);

        var func_name = '_onUbus_' + method_name;
        var func = self[func_name];
        var res_str;
        if (func) {
            res_str = JSON.stringify(func(JSON.parse(req_str)));
            console.log('res: ' + res_str);
        } else {
            console.log('ubus method not implemented, not calling');
            res_str = JSON.stringify(ubus_success());
        }

        return res_str;
    };
}
util.inherits(BeepApp, beep.BeepApp);

BeepApp.prototype.beepSendState = function(event_type, event_data, state) {
    return this.nativeBeepApp._beepSendState(
            event_type,
            JSON.stringify(event_data),
            JSON.stringify(state));
};

var ubus_error = function(msg, code) {
    return {
        'success': false,
        'error_code': code,
        'error_message': msg
    }
};
var ubus_success = function(result) {
    if (!result) {
        result = {__unused:0};
    }
    return {
        'success': true,
        'result': result
    }
};

var log_all = function(msg) {
    var chunk_size = 256
    console.log('LOGALL-START');
    for (var i = 0; i < msg.length; i += chunk_size) {
        console.log('LOGALL: ' + msg.slice(i, i + chunk_size));
    }
    console.log('LOGALL-END');
}

exports.BeepApp = BeepApp;
exports.ubus_error = ubus_error;
exports.ubus_success = ubus_success;
exports.log_all = log_all;
