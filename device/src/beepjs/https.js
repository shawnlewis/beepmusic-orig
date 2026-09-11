// For some reason directly setting this module's exports to another module's
// exports does not work.
var http = require('http');
exports.request = http.request;
exports.get = http.get;
http._curl_protocol = 'https';
exports._curl_protocol = http._curl_protocol;

