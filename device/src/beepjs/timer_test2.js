/*
 * This reproduces the libuv core bug tracked under BEEP-429
 */
var t;

var tcb = function() {
    console.log('tcb')
    clearTimeout(t);
    console.log('t cleared')
};

t = setTimeout(tcb, 1);
