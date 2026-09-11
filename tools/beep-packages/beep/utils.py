import commands
import os
import time

from multiprocessing import pool

class CommandError(Exception):
    pass

def quick_cmd(cmd):
    s, o = commands.getstatusoutput(cmd)
    if (s != 0):
        raise CommandError('Error: \'{}\' exited with code: {}'.format(cmd, s))
    return o

def millis():
    return(int(time.time() * 1000))

def staging_dir():
    return os.path.realpath(os.path.join(os.path.split(__file__)[0],
            '../../../device/openwrt/staging_dir'))

def root_dir():
    return os.path.join(staging_dir(),
            'target-mips_r2_uClibc-0.9.33.2/root-ar71xx')

def build_dir():
    return os.path.realpath(os.path.join(os.path.split(__file__)[0],
            '../../../device/openwrt/build_dir'))

def toolchain_dir():
    return os.path.realpath(os.path.join(os.path.split(__file__)[0],
            '../../../device/openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin'))

def get_tool(tool):
    return os.path.join(toolchain_dir(), 'mips-openwrt-linux-uclibc-' + tool)

def parallel_do(callbacks):
    tp = pool.ThreadPool(processes=20)
    results = []
    for cb in callbacks:
        results.append(tp.apply_async(cb))
    ret = []
    for r in results:
        try:
            ret.append(r.get())
        except OSError:
            ret.append(None)
    tp.terminate()
    tp.close()
    return ret

def make_closure(func, *args):
    def run():
        return func(*args)
    return run

def hex_dump(d):
    offset = 0
    for x in xrange(0, len(d), 16):
        hex_line = []
        for p in xrange(2):
            line = d[x + (8 * p):x + (8 * p) + 8]
            line = [ord(c) for c in line]
            fmt = ' '.join(['{:02x}',] * len(line))
            hex_line.append(fmt.format(*line))
        hex_line = '  '.join(hex_line)
        print('0x{:04x}: {}'.format(offset, hex_line))
        offset += 16

def hex_str_to_bin(s):
    return ''.join([chr(int(s[x:x + 2], 16)) for x in range(0, len(s), 2)])

def bin_to_hex_str(b):
    return ''.join(['{:02x}'.format(ord(x)) for x in b])
