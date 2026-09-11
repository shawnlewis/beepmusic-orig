
console.log('bla');

var intervals = [];

function do_something() {
    var val = Math.random();
    if (Math.random() < .3) {
        remove_interval();
    } else if (val < .6) {
        new_interval();
    } else {
        new_timeout();
    }
}

function new_interval() {
    intervals.push(setInterval(function() {
        do_something();
    }, Math.floor(Math.random() * 1000)));
}

function new_timeout() {
    intervals.push(setTimeout(function() {
        do_something();
    }, Math.floor(Math.random() * 1000)));
}

function remove_interval() {
    var interval_index = Math.floor(Math.random() * (intervals.length - 1) + 1);
    clearInterval(intervals[interval_index]);
    clearInterval(intervals[interval_index]);
    intervals.splice(interval_index, 1);
}

do_something();
