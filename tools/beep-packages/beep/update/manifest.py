import os
import yaml

from beep.update.constants import *
import beep.update.verify
import beep.utils

class YamlHexUint32(int):
    pass

# Need to use this to prevent yaml from adding the '' around the
# string number.
def yaml_hex_repr(dumper, data):
    return yaml.ScalarNode('tag:yaml.org,2002:int', '0x{:08x}'.format(data))

class Manifest(object):
    need_repr = True

    def __init__(self):
        if self.need_repr is True:
            yaml.add_representer(YamlHexUint32, yaml_hex_repr)
            Manifest.need_repr = False
        self.files = []

    def __len__(self):
        return len(self.files)

    def __str__(self):
        t = {
            'manifest': { 'millis': beep.utils.millis() },
            'files': self.files
        }
        return yaml.dump(t, explicit_start=True, default_flow_style=False)

    def _validate_flags(self, ftype, flags):
        # Only remove currently has flags.
        if ftype == 'rm' and (flags & REMOVE_FLAGS['INVALID']) == 0:
            return
        raise ValueError('flags \'0x{:08x}\' invalid for type \'{}\''.format(
                flags, ftype))

    def _mkfile(self, ftype, path, flags, method, realpath):
        if method not in DIG_METHODS.keys():
            raise ValueError('unknown digest {}'.format(method))

        if realpath == None:
            realpath = path

        # Need to check for link first.
        if os.path.islink(path):
            # Link digest is based on the manifest path since during
            # a partial update the link may not actually point to a
            # real file.  Tar will archive symlinks as special files
            # and does not actually follow the link.
            digest_on_path = True
            digest_path = path
        elif os.path.isfile(path):
            digest_on_path = False
            digest_path = realpath
        else:
            raise IOError('{} is not a file or link'.format(realpath))

        digest = beep.update.verify.Digest(digest_path, 'manifest',
                digest_on_path=digest_on_path)
        digest.add(method)

        ret = {
            'path': path,
            'digest': digest.manifest_list(),
            'type': ftype
        }
        if flags not in [0, None]:
            self._validate_flags(ftype, flags)
            ret['flags'] = YamlHexUint32(flags)
        return ret

    def _rmfile(self, path, flags):
        ret = {
            'path': path,
            'type': 'rm'
        }
        if flags not in [0, None]:
            self._validate_flags('rm', flags)
            ret['flags'] = YamlHexUint32(flags)
        return ret

    def _mkentry(self, ftype, path, **kwargs):
        if ftype not in MANIFEST_FILE_TYPES:
            raise ValueError('unknown file type {}'.format(ftype))

        if ftype == 'rm':
            return self._rmfile(path, kwargs.get('flags', None))
        else:
            return self._mkfile(ftype, path,
                    kwargs.get('flags', None),
                    kwargs['method'],
                    kwargs.get('realpath', None))

    def _index(self, ftype, path):
        # Add ftype wildcard for searching only, which will match the first
        # matching path.
        if ftype not in MANIFEST_FILE_TYPES + ['*']:
            raise ValueError('unknown file type {}'.format(ftype))

        for i, v in enumerate(self.files):
            if v['path'] == path and (ftype == '*' or v['type'] == ftype):
                return i
        return -1

    def index(self, ftype, path):
        i = self._index(ftype, path)
        if i != -1:
            return i
        raise ValueError('\'{}\' not in files with type \'{}\''.format(
                path, ftype))

    def append(self, ftype, path, **kwargs):
        i = self._index(ftype, path)
        if i == -1:
            return self.files.append(self._mkentry(ftype, path, **kwargs))
        raise ValueError('\'{}\' already in files with type \'{}\''.format(
                path, ftype))

    def insert(self, index, ftype, path, **kwargs):
        i = self._index(ftype, path)
        if i == -1:
            return self.files.insert(index,
                    self._mkentry(ftype, path, **kwargs))
        raise ValueError('\'{}\' already in files with type \'{}\''.format(
                path, ftype))

    def remove(self, ftype, path):
        i = self._index(ftype, path)
        if i != -1:
            return self.files.pop(i)
        raise ValueError('\'{}\' not in files with type \'{}\''.format(
                path, ftype))

    def list_type_path(self):
        # These must be in each file entry.
        return [(x['type'], x['path']) for x in self.files]
