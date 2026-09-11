import os
import struct

from beep.update.constants import *
from beep.utils import quick_cmd, hex_str_to_bin, bin_to_hex_str

# magic will just return ascii for these files.
def _style_from_ext(path):
    if path.endswith('.lua'):
        return 'lua'
    elif path.endswith('.sh'):
        return 'sh'
    elif path.endswith('.yml'):
        return 'yml'
    else:
        return 'bin'

class VerifyError(Exception):
    pass

# returns id in a hex string.
def _id_pem_path_from_key_file(path):
    keyfile = open(path, 'r')
    data = keyfile.read()
    keyfile.close()

    data = data.strip()
    key_id, pem_path = data.split(' ')

    path_dir = os.path.split(os.path.abspath(os.path.expanduser(path)))[0]
    pem_path = os.path.abspath(os.path.join(path_dir, pem_path))

    return key_id, pem_path

class Digest(object):
    def __init__(self, path, style, load_from_file=False,
            digest_on_path=False, key_paths=None):
        if style not in DIG_STYLE:
            raise VerifyError('Invalid digest style \'{}\''.format(style))

        if not os.path.isfile(path) and digest_on_path is False:
            raise VerifyError('\'{}\' is not a file'.format(path))

        if digest_on_path is True and style != 'manifest':
            raise VerifyError('digest_on_path only available if style == ' \
                    'manifest')

        if key_paths is not None:
            bad_paths = [x for x in key_paths if not os.path.isfile(x)]
            if len(bad_paths) != 0:
                raise VerifyError('key_file \'{}\' is not a file'.format(
                        bad_paths[0]))

        if load_from_file is True:
            raise VerifyError('not supported')

        # Sanity short and int.
        if struct.calcsize('>H') != 2 or struct.calcsize('>I') != 4:
            raise VerifyError('struct sizes are incorrect on this platform')

        self.path = path
        # Format: (method, digest)
        self.dig_list = []
        self.style = style
        self.load_from_file = load_from_file
        self.digest_on_path = digest_on_path
        self.key_paths = key_paths

    def _dig_entry_to_str(self, entry):
        method, digest, _ = entry
        return '{}:{:04x}:{}'.format(DIG_METHODS[method], len(digest), digest)

    def _dig_entry_to_bin(self, entry):
        method, digest, _ = entry
        digest = hex_str_to_bin(digest)
        size = struct.pack('>H', len(digest))
        return '{}:{}:{}'.format(DIG_METHODS[method], size, digest)

    def _add_signature(self, method, key_path):
        key_id, pem_path = _id_pem_path_from_key_file(key_path)
        if not os.path.isfile(pem_path):
            raise VerifyError('\'{}\' is not a file'.format(repr(pem_path)))

        # Remove 'signed-' from the method.
        sign_method = method.split('-')[1]

        if self.digest_on_path is False:
            digest = quick_cmd('openssl dgst -{} -sign {} -hex {}'.format(
                    sign_method, pem_path, self.path))
        else:
            digest = quick_cmd('echo {} | openssl dgst -{} -sign {} ' \
                    '-hex'.format(self.path, sign_method, pem_path))
        digest = key_id + digest.split(' ')[1]

        self.dig_list.append((method, digest, key_id))

    def _add_signatures(self, method):
        if self.key_paths is None:
            raise VerifyError('key_path required for signatures')

        for key_path in self.key_paths:
            self._add_signature(method, key_path)

    def _add_hash(self, method):
        if self.digest_on_path is False:
            digest = quick_cmd('openssl {} {}'.format(method, self.path))
        else:
            digest = quick_cmd('echo {} | openssl {}'.format(
                    self.path, method))
        digest = digest.split(' ')[1]

        if len(digest) != DIG_SIZE[method]:
            raise VerifyError('Expected hash size {} got {}'.format(
                    DIG_SIZE[method], len(digest)))

        self.dig_list.append((method, digest, None))

    def __str__(self):
        if self.style == 'manifest':
            raise VerifyError('Can not generate string for manifest style')

        prefix, postfix, is_str = STYLE_INFO[self.style]

        # assemble all of the data.
        digest_data = []
        for entry in self.dig_list:
            if is_str:
                data = self._dig_entry_to_str(entry)
            else:
                data = self._dig_entry_to_bin(entry)
            digest_data.append('{}{}{}'.format(prefix, data, postfix))
        digest_data = ''.join(digest_data)

        # create the header.
        if is_str:
            magic = bin_to_hex_str(SIGNED_MAGIC)
            hdr = '{}{}:{:08x}{}'.format(prefix, magic, len(digest_data),
                    postfix)
        else:
            size = struct.pack('>I', len(digest_data))
            hdr = '{}{}:{}{}'.format(prefix, SIGNED_MAGIC, size, postfix)

        return hdr + digest_data

    def add(self, method):
        if method not in DIG_METHODS.keys():
            raise VerifyError('Invalid digest method \'{}\''.format(method))

        if method.startswith('signed-'):
            return self._add_signatures(method)
        else:
            return self._add_hash(method)

    def remove(self, index):
        if index >= len(self.dig_list):
            raise VerifyError('index {} out of range'.format(index))
        self.dig_list.pop(index)

    def count(self):
        return len(self.dig_list)

    def dump(self):
        for dig_entry in self.dig_list:
            print(self._dig_entry_to_str(dig_entry))

    def manifest_list(self):
        return [self._dig_entry_to_str(x) for x in self.dig_list]

    def embed(self, path=None, verbose=False):
        inpath = self.path
        outpath = path or self.path

        if verbose is True:
            print('Signing {} with'.format(inpath))
            for method, _, key_id in self.dig_list:
                if key_id is None:  # hash
                    print('Hash {}'.format(method))
                else:
                    print('Signature {} using id {}'.format(method, key_id))

        # Read in all data in case inpath == outpath.
        infile = open(inpath, 'r')
        data = infile.read()
        infile.close()

        outfile = open(outpath, 'w')
        outfile.write(str(self))
        outfile.write(data)
        outfile.close()

# This only checks for the magic number and is not really safe.  It
# should be part of the Digest class once reading digests is done.
def del_signature(inpath, outpath, style=None):
    if style is None:
        style = _style_from_ext(inpath)

    prefix, postfix, is_str = STYLE_INFO[style]

    if is_str is True:
        hdr_size = 17
    else:
        hdr_size = 9

    hdr_size += len(prefix) + len(postfix)

    infile = open(inpath, 'r')
    data = infile.read()
    infile.close()

    header = data[:hdr_size]

    if not header.startswith(prefix) or not header.endswith(postfix):
        raise VerifyError('incorrect prefix/postfix')

    # Strip prefix and postfix.  Check postfix as -0 will return an empty
    # string.
    header = header[len(prefix):]
    if len(postfix):
        header = header[:-len(postfix)]

    magic, digest_size = header.split(':')

    if is_str is True:
        magic = hex_str_to_bin(magic)
        digest_size = int(digest_size, 16)
    else:
        digest_size = struct.unpack('>I', digest_size)[0]

    if magic != SIGNED_MAGIC:
        raise VerifyError('bad magic number')

    data = data[hdr_size + digest_size:]
    outfile = open(outpath, 'w')
    outfile.write(data)
    outfile.close()
