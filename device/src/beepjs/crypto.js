var binding = beepjs.natives.crypto;

exports.createHash = exports.Hash = Hash;
function Hash(md_type, opts) {
    if (!(this instanceof Hash)) {
        return new Hash(md_type, opts);
    }
    this._binding = new binding.Hash(md_type);
};

Hash.prototype.update = function(data, enc) {
    enc = enc || 'buffer';
    if (enc === 'buffer' && typeof data === 'string') {
        enc = 'binary';
    }
    this._binding.update(data, enc);
    return this;
};

Hash.prototype.digest = function(enc) {
    enc = enc || 'buffer';
    return this._binding.digest(enc);
}

