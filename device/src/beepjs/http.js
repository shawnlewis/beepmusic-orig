var util = require('util');
var url = require('url');
var EventEmitter = require('events').EventEmitter;

var default_opts = {
    method: 'GET',
    path: '',
    headers: {}
};

var unsupported_opts = [
    'localAddress',
    'socketPath',
    'auth',
    'agent'
];

function set_curl_opts(curl, opts) {
    var safe_opts = {};
    for (var key in opts) {
        if (unsupported_opts.indexOf(key) > -1) {
            console.warn('Ignoring unsupported option: ' + key);
        } else {
            safe_opts[key] = opts[key]
        }
    }
    for (var key in default_opts) {
        safe_opts[key] = safe_opts[key] || default_opts[key];
    }

    //curl.setopt(curl.CURLOPT_USERAGENT, 'libcurl-agent/1.0');

    if (exports._curl_protocol === 'http') {
        safe_opts.port = safe_opts.port || 80;
    } else if (exports._curl_protocol === 'https') {
        safe_opts.port = safe_opts.port || 443;
        if (process.arch === 'mips') {
            curl.setopt(curl.CURLOPT_SSL_VERIFYPEER, 0);
        }
    } else {
        throw new Error('unsupported protocol: ' + exports._curl_protocol);
    }
    if (safe_opts.host && !safe_opts.host_name) {
        safe_opts.hostname = safe_opts.host;
    }
    if (!safe_opts.hostname) {
        throw new Error('hostname not specified');
    }
    if (safe_opts.path[0] != '/') {
        safe_opts.path = '/' + safe_opts.path;
    }
    safe_opts._url = exports._curl_protocol + '://' + safe_opts.hostname
            + ':' + safe_opts.port + safe_opts.path;
    curl.setopt(curl.CURLOPT_URL, safe_opts._url);

    if (safe_opts.method === 'POST') {
        curl.setopt(curl.CURLOPT_POST, 1);
        safe_opts.headers['Transfer-Encoding'] = 'chunked';
        safe_opts.headers['Expect'] = '';
        safe_opts.headers['Content-Type'] = '';
    } else if (safe_opts.method !== 'GET') {
        throw new Error('unsupported method: ' + safe_opts.method);
    }

    if (Object.keys(safe_opts.headers).length > 0) {
        curl.setopt(curl.CURLOPT_HTTPHEADER, safe_opts.headers);
    }

    if (safe_opts.verbose) {
        curl.setopt(curl.CURLOPT_VERBOSE, 1);
    }

    return safe_opts;
};

exports._curl_protocol = 'http';

var CRLF = '\r\n';

function ClientResponse(headers) {
    this.statusCode = 0;
    this.headers = {}
    this.enc = 'buffer';
    for (var i in headers) {
        var header = headers[i];
        if (header.search('HTTP/1.') == 0) {
            this.statusCode = parseInt(header.split(' ')[1]);
        } else if (header.search(':') > -1) {
            var key = header.slice(0, header.search(':')).trim();
            var val = header.slice(header.search(':') + 1).trim();
            if (key !== '') {
                this.headers[key.toLowerCase()] = val.toLowerCase();
            }
        }
    }
}
util.inherits(ClientResponse, EventEmitter);

ClientResponse.prototype.setEncoding = function(enc) {
    this.enc = enc;
};

ClientResponse.prototype._onWriteCallback = function(curl_buf) {
    if (this.enc === 'buffer') {
        this.emit('data', curl_buf);
    } else {
        var chunk = curl_buf.toString(this.enc, 0, curl_buf.length);
        this.emit('data', chunk)
    }
    return curl_buf.length;
};

function ClientRequest(opts, cb) {
    var self = this;
    var headers = [];
    this.res_cb = cb;
    this.curl = beepjs.natives.curl.Curl();
    this.opts = set_curl_opts(this.curl, opts);
    this.aborted = false;
    this.paused = false;
    this.curl._onHeaderCallback = function(curl_buf) {
        var chunk = curl_buf.toString('utf8', 0, curl_buf.length);
        if (chunk !== CRLF) {
            headers.push(chunk)
        } else {
            if (curl_buf.length == 2) {
                self.res = new ClientResponse(headers);
                self.curl._onWriteCallback = function(curl_buf) {
                    return self.res._onWriteCallback(curl_buf);
                }
                self.res_cb(self.res);
            }
        }
        return curl_buf.length;
    }
    this.curl._onCurlError = function(message) {
        var error = new Error(message);
        self.emit('error', error);
    };
    this.curl._onCurlClose = function() {
        if (self.res) {
            self.res.emit('close');
        }
    };
}
util.inherits(ClientRequest, EventEmitter);

// Node will open a tcp socket when 'request' is called and wait until 'end' to
// request.  Writes will get processed immediately (the first write will also
// write headers).  To get as close as possible this will defer perform until
// either a write is made or end is called.  The two remaining differences is
// a request can cause a error event (timeout/connection refused) or
// incorrectly calling write during a get method will start the get request.
// It's also worth noting a write during a get method in node will actually
// send data where curl will not.
ClientRequest.prototype.write = function(chunk, enc) {
    if (enc && enc !== 'utf8') {
        throw new Error('unsupported encoding: ' + enc);
    }
    if (!this.performed) {
        this.performed = true;
        this.curl.perform();
    }
    this.curl.write(chunk);
};

ClientRequest.prototype.end = function() {
    if (!this.performed) {
        this.performed = true;
        this.curl.perform();
    }
    this.curl.end();
};

ClientRequest.prototype.abort = function() {
    this.aborted = true;
    this.curl.abort();
    // The abort has to be processed from one of the socket callbacks so
    // unpause so uv will poll the socket again causing the abort signal to
    // curl. This may cause an issue if the socket is stuck without any data.
    if (this.paused) {
        this.curl.pause(false);
    }
};

ClientRequest.prototype.pause = function () {
    if (this.performed && !this.paused) {
        this.paused = true;
        this.curl.pause(this.paused);
    }
};

ClientRequest.prototype.resume = function () {
    if (this.paused) {
        this.paused = false;
        this.curl.pause(this.paused);
    }
};

exports.request = function(opts, cb) {
    if (typeof opts == 'string') {
        opts = url.parse(opts);
    }
    return new ClientRequest(opts, cb);
};

exports.get = function(opts, cb) {
    var req = exports.request(opts, cb);
    req.end();
    return req;
};

