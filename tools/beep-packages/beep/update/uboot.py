import binascii
import os
import struct
import tempfile

from beep.utils import quick_cmd

# Definitions from u-boot-2013.04/include/image.h
IMG_HDR_T_STRUCT = '>IIIIIIIBBBB32s'

IMG_HDR_MEMBERS = [
    'magic',
    'hcrc',
    'time',
    'size',
    'load',
    'ep',
    'dcrc',
    'os',
    'arch',
    'type',
    'comp',
    'name'
]

IMG_HDR_MAGIC = 0x27051956

IMG_HDR_OS = {
    0: 'INVALID',
    1: 'OPENBSD',
    2: 'NETBSD',
    3: 'FREEBSD',
    4: '4_4BSD',
    5: 'LINUX',
    6: 'SVR4',
    7: 'ESIX',
    8: 'SOLARIS',
    9: 'IRIX',
    10: 'SCO',
    11: 'DELL',
    12: 'NCR',
    13: 'LYNXOS',
    14: 'VXWORKS',
    15: 'PSOS',
    16: 'QNX',
    17: 'U_BOOT',
    18: 'RTEMS',
    19: 'ARTOS',
    20: 'UNITY',
    21: 'INTEGRITY',
    22: 'OSE',
    23: 'PLAN9'
}

IMG_HDR_ARCH = {
    0: 'INVALID',
    1: 'ALPHA',
    2: 'ARM',
    3: 'I386',
    4: 'IA64',
    5: 'MIPS',
    6: 'MIPS64',
    7: 'PPC',
    8: 'S390',
    9: 'SH',
    10: 'SPARC',
    11: 'SPARC64',
    12: 'M68K',
    14: 'MICROBLAZE',
    15: 'NIOS2',
    16: 'BLACKFIN',
    17: 'AVR32',
    18: 'ST200',
    19: 'SANDBOX',
    20: 'NDS32',
    21: 'OPENRISC'
}

IMG_HDR_TYPE = {
    0: 'INVALID',
    1: 'STANDALONE',
    2: 'KERNEL',
    3: 'RAMDISK',
    4: 'MULTI',
    5: 'FIRMWARE',
    6: 'SCRIPT',
    7: 'FILESYSTEM',
    8: 'FLATDT',
    9: 'KWBIMAGE',
    10: 'IMXIMAGE',
    11: 'UBLIMAGE',
    12: 'OMAPIMAGE',
    13: 'AISIMAGE',
    14: 'KERNEL_NOLOAD',
    15: 'PBLIMAGE'
}

IMG_HDR_COMP = {
    0: 'NONE',
    1: 'GZIP',
    2: 'BZIP2',
    3: 'LZMA',
    4: 'LZO'
}

READ_BS = 4096

SQUASHFS_MAGIC = 0x73717368

def _parse_header(raw_hdr):
    hdr_vals = struct.unpack(IMG_HDR_T_STRUCT, raw_hdr)
    hdr = dict(zip(IMG_HDR_MEMBERS, hdr_vals))

    if hdr['magic'] != IMG_HDR_MAGIC:
        raise Exception('Bad magic number: 0x{:08x}'.format(hdr['magic']))

    # Add names to known fields.
    hdr['os'] = IMG_HDR_OS.get(hdr['os'], 'unknown')
    hdr['arch'] = IMG_HDR_ARCH.get(hdr['arch'], 'unknown')
    hdr['type'] = IMG_HDR_TYPE.get(hdr['type'], 'unknown')
    hdr['comp'] = IMG_HDR_COMP.get(hdr['comp'], 'unknown')

    # Trim trailing NULL chars from name.
    hdr['name'] = hdr['name'].strip('\x00')

    return hdr

def img_header(path):
    img_f = open(path, 'r')
    raw_hdr = img_f.read(struct.calcsize(IMG_HDR_T_STRUCT))
    img_f.close()
    return _parse_header(raw_hdr)

def verify_image(path):
    img_f = open(path, 'r')
    raw_hdr = img_f.read(struct.calcsize(IMG_HDR_T_STRUCT))
    hdr = _parse_header(raw_hdr)

    kernel = img_f.read(hdr['size'])
    img_f.close()

    # Zero out the crc field.
    raw_hdr = raw_hdr[:4] + '\x00\x00\x00\x00' + raw_hdr[8:]

    # binascii.crc32 uses the same algorithm/poly as uboot.
    hdr_crc = binascii.crc32(raw_hdr) & 0xffffffff
    if hdr_crc != hdr['hcrc']:
        return False

    kernel_crc = binascii.crc32(kernel) & 0xffffffff
    if kernel_crc != hdr['dcrc']:
        return False

    return True

def extract_kernel(path, dest, decompress):
    img_f = open(path, 'r')
    raw_hdr = img_f.read(struct.calcsize(IMG_HDR_T_STRUCT))
    hdr = _parse_header(raw_hdr)

    kernel = img_f.read(hdr['size'])
    img_f.close()

    if decompress is True:
        dest = dest + '.lzma'

    kernel_f = open(dest, 'w')
    kernel_f.write(kernel)
    kernel_f.close()

    if decompress is True:
        quick_cmd('lzma -d {}'.format(dest))

def _read_until_nonzero(f):
    zeros = '\x00' * READ_BS
    data = zeros

    while data == zeros:
        data = f.read(READ_BS)
        # At end of the file.
        if len(data) != READ_BS:
            break

    offset = 0
    for x in xrange(len(data)):
        if data[x] != '\x00':
            offset = x
            break

    f.seek(offset - len(data), 1)

def extract_squashfs(path, dest, decompress):
    img_f = open(path, 'r')
    raw_hdr = img_f.read(struct.calcsize(IMG_HDR_T_STRUCT))
    hdr = _parse_header(raw_hdr)

    # Go to the end of the kernel
    img_f.seek(hdr['size'], 1)

    # Read until the beginning of squashfs.
    _read_until_nonzero(img_f)

    # Don't really need to parse the squashfs header, but check the magic
    # number and get the size.
    fs_pos = img_f.tell()

    fs_magic = struct.unpack('<I', img_f.read(4))[0]
    if fs_magic != SQUASHFS_MAGIC:
        raise Exception('Bad magic number: 0x{:08x}'.format(fs_magic))

    # The size is at offset 0x28 but we already read 4 bytes from the
    # beginning of the superblock.
    img_f.seek(0x24, 1)
    fs_size = struct.unpack('<Q', img_f.read(8))[0]

    img_f.seek(fs_pos)
    fs_data = img_f.read(fs_size)
    img_f.close()

    # If decompressing extract the squashfs to a temp file.
    if decompress is True:
        if os.path.isdir(dest) or os.path.isfile(dest):
            raise Exception('dest: \'{}\' must not exist when ' \
                    'decompress is True'.format(dest))
        _dest = tempfile.mkstemp()[1]
    else:
        _dest = dest

    fs_f = open(_dest, 'w')
    fs_f.write(fs_data)
    fs_f.close()

    # If decompressing extract the contents of the squashfs to the dest
    # and remove the temp file.
    if decompress is True:
        quick_cmd('unsquashfs -d {} {}'.format(dest, _dest))
        os.unlink(_dest)
