import platform


class TargetDefinition(object):
    def __init__(self, name, arch, toolchain, endianness, device_type='real'):
        self.name = name
        self.arch = arch
        self.toolchain = toolchain
        self.endianness = endianness
        self.device_type = device_type

    def __repr__(self):
        keys = ['name', 'arch', 'toolchain', 'endianness', 'device_type']
        vals = [self.__getattribute__(x) for x in keys]
        return 'Target Definition: {}'.format(
                ', '.join(['='.join(x) for x in zip(keys, vals)]))

    def __str__(self):
        return self.__repr__()


class TargetDatabase(object):
    def __init__(self):
        self._db = {}

    def __repr__(self):
        return '\n'.join([str(self._db[x]) for x in self.list_targets()])

    def __str__(self):
        return self.__repr__()

    def add_target(self, name, arch, toolchain, endianness,
            device_type='real'):
        if self.has_target(name):
            raise Exception('target \'{}\' already exists'.format(name))
        self._db[name] = TargetDefinition(name, arch, toolchain, endianness,
                device_type)

    def has_target(self, name):
        return self._db.has_key(name)

    def get_target(self, name):
        if not self.has_target(name):
            raise Exception('unknown target \'{}\''.format(name))
        return self._db[name]

    def list_targets(self):
        return sorted(self._db.keys())

    def dump(self):
        print str(self)


targetdb = TargetDatabase()

targetdb.add_target('host', platform.machine(), 'host', 'little', 'virtual')
targetdb.add_target('beepone', 'mips', 'openwrt-mips-r2', 'big')
targetdb.add_target('beeptwo', 'mips', 'openwrt-mips-r2', 'big')
