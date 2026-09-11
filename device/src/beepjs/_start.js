// Get the basic system initialized and execute a script from the command line
// args.
(function (beepjs) {
    /// Console
    function Console() {
        if (!(this instanceof Console)) {
            return new Console()
        }
        Object.keys(Console.prototype).forEach(function(key) {
            this[key] = this[key].bind(this);
        }, this);
    }

    if (typeof(beepjs.natives.beep) == 'undefined' ||
            beepjs.natives.process.execArgv.indexOf('--no-beep-log') > -1) {
        Console.prototype.log = function() {
            var args = ['DEBUG: '] + Array.prototype.slice.call(arguments) + ['\n'];
            beepjs.natives.system.stdout.write(args);
        };

        Console.prototype.info = function () {
            var args = ['INFO: '] + Array.prototype.slice.call(arguments) + ['\n'];
            beepjs.natives.system.stdout.write(args);
        }

        Console.prototype.warn = function() {
            var args = ['WARN: '] + Array.prototype.slice.call(arguments) + ['\n'];
            beepjs.natives.system.stderr.write(args);
        };

        Console.prototype.error = function() {
            var args = ['ERROR: '] + Array.prototype.slice.call(arguments) + ['\n'];
            beepjs.natives.system.stderr.write(args);
        };
    } else {
        Console.prototype.log = function() {
            var args = Array.prototype.slice.call(arguments);
            beepjs.natives.beep.log(
                beepjs.natives.beep.LOG_PRIORITY_DEBUG, args);
        };

        Console.prototype.info = function() {
            var args = Array.prototype.slice.call(arguments);
            beepjs.natives.beep.log(
                beepjs.natives.beep.LOG_PRIORITY_INFO, args);
        };

        Console.prototype.warn = function() {
            var args = Array.prototype.slice.call(arguments);
            beepjs.natives.beep.log(
                beepjs.natives.beep.LOG_PRIORITY_WARN, args);
        };

        Console.prototype.error = function() {
            var args = Array.prototype.slice.call(arguments);
            beepjs.natives.beep.log(
                beepjs.natives.beep.LOG_PRIORITY_ERROR, args);
        };
    }

    /// Timer
    var _runningTimers = [];

    var _removeTimer = function(timer) {
        var timerIndex = _runningTimers.indexOf(timer);
        //beepjs.natives.beep.log(
        //        beepjs.natives.beep.LOG_PRIORITY_DEBUG,
        //        'Timer list length: ' + _runningTimers.length +
        //        ', Removing timer at index: ' + timerIndex);
        if (timerIndex !== -1) {
            _runningTimers.splice(timerIndex, 1);
            return true;
        } else {
            beepjs.natives.beep.log(
                    beepjs.natives.beep.LOG_PRIORITY_WARN,
                    'WOAH Removing a timer we don\'t have');
            return false;
        }
    }

    var _setTimer = function(callback, timeout, repeat) {
        var timer = new beepjs.natives.timer.Timer();
        if (arguments.length <= 3) {
            timer._onTimeout = function() {
                callback.apply(timer);
                if (!repeat) {
                    _removeTimer(timer);
                }
            }
        } else {
            var cb_args = Array.prototype.slice.call(arguments, 3);
            timer._onTimeout = function() {
                callback.apply(timer, cb_args);
                if (!repeat) {
                    _removeTimer(timer);
                }
            }
        }
        timer.start(timeout, repeat);
        _runningTimers.push(timer);
        return timer;
    };
    var _clearTimer = function(timer) {
        if (timer instanceof beepjs.natives.timer.Timer) {
            if (_removeTimer(timer)) {
                timer.stop()
            }
        }
    };
    var _clearAllTimers = function() {
        for (var i in _runningTimers) {
            _runningTimers[i].stop();
        }
        _runningTimers = [];
    };

    var _globals = {
        setTimeout: function(callback, timeout) {
            return _setTimer.apply(this, [callback, timeout, 0].concat(
                    Array.prototype.slice.call(arguments, 2)));
        },
        setInterval: function(callback, repeat) {
            return _setTimer.apply(this, [callback, repeat, repeat].concat(
                    Array.prototype.slice.call(arguments, 2)));
        }
    }

    // calls the version in globals so that it can be overridden.
    var _setTimeout = function(callback, timeout) {
        return _globals.setTimeout.apply(this, arguments);
    };

    var _clearTimeout = _clearTimer;

    // calls the version in globals so that it can be overridden.
    var _setInterval = function(callback, repeat) {
        return _globals.setInterval.apply(this, arguments);
    };

    var _clearInterval = _clearTimer;

    var _require = function(name) {
        if (!name.match('.js$')) {
            name += '.js';
        }
        return execScript(name);
    };

    var Module = function() {
        this.beepjs = beepjs;
        this.process = beepjs.natives.process;
        this.console = Console();
        this.setTimeout = _setTimeout;
        this.clearTimeout = _clearTimeout;
        this.setInterval = _setInterval;
        this.clearInterval = _clearInterval;
        this.globals = _globals;
        this._clearAllTimers = _clearAllTimers;
        this.require = _require;
        this.Buffer = beepjs.natives.buffer.Buffer;
        //this.module = this;
        this.exports = {};
    };

    var execScript = function(path) {
        // Setup scope object to execute the script in.
        var module = new Module();
        beepjs.natives.system.execScript(path, module);
        return module.exports;
    };

    var main = function() {
        if (beepjs.natives.process.argv.length < 2) {
            beepjs.natives.system.stderr.write(
                    'No javascript file to execute.\n');
            beepjs.natives.process.exit(1);
        }
        execScript(beepjs.natives.process.argv[1]);
    };

    // GO!
    main();
} (beepjs));
