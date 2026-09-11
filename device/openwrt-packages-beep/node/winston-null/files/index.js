var Logger = exports.Logger = function Logger(args) {
}

exports.log = Logger.prototype.log = function(msg) {
    console.log(msg);
}

exports.info = Logger.prototype.info = function(msg) {
    console.log(msg);
}

exports.error = Logger.prototype.error = function(msg) {
    console.log(msg);
}

