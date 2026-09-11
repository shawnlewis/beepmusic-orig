#!/usr/bin/env python

import argparse
import os
import sys
import commands
import struct

from beep.utils import quick_cmd

try:
    import argcomplete
except:
    pass

BEEPVER_1_VER_SYMBOL = 'beep_version'
BEEPVER_1_VER_STRUCT = '{endian}II{ptr}{ptr}{ptr}{ptr}'
BEEPVER_1_VER_MEMBERS = [
    ('tip_ver',     False),
    ('flags',       False),
    ('builder',     True),
    ('date',        True),
    ('gcc_ver',     True),
    ('git_rev',     True)
]

BEEPVER_2_VER_STRUCT = '>II33s33s33s41s'
BEEPVER_2_VER_MEMBERS = BEEPVER_1_VER_MEMBERS

VER_FLAGS = [
    'OPENWRT',
    'GIT_CLEAN',
    'GIT_DIRTY'
]
STR_SECTION = '.rodata'
STR_MAX = 100

# There doesn't seem to be solid bindings for bfd so we'll just use commands
# and parse the output.
def get_elf_header(path):
    return quick_cmd('readelf -h {}'.format(path))

def get_sec_header(path):
    return quick_cmd('readelf -S -W {}'.format(path))

def get_ptr_size(elf_hdr):
    elf_class = elf_hdr.split('\n')[2].split(':')[1].strip()
    if (elf_class == 'ELF32'):
        return 4
    elif (elf_class == 'ELF64'):
        return 8
    raise Exception('unknown elf class: {}'.format(elf_class))

def get_type(elf_hdr):
    elf_type = elf_hdr.split('\n')[7].split(':')[1].strip()
    elf_type = elf_type.split(' ')[0]
    if (elf_type == 'EXEC') or (elf_type == 'DYN'):
        return elf_type
    raise Exception('unknown elf class: {}'.format(elf_type))

def get_endian(elf_hdr):
    elf_type = elf_hdr.split('\n')[3].split(':')[1].strip()
    if (elf_type.find('little') != -1):
        return 'little'
    if (elf_type.find('big') != -1):
        return 'big'
    raise Exception('unknown elf type: {}'.format(elf_type))

# returns vma, section, size
def get_sym_info(path, sym_name):
    line = quick_cmd('objdump -t {} | grep -E \' {}$\''.format(path, sym_name))
    line = line.split('\n')
    if len(line) == 0:
        raise Exception('sym {} not found in {}'.format(sym_name, path))
    elif len(line) > 1:
        print('Warning multiple matches for sym {} in {}'.format(sym_name,
                path))
    # There is always a \t between the section name and size
    # (see bfd_elf_print_symbol).
    line = line[0].split('\t')
    vma = line[0].split(' ')[0]
    sec = line[0].split(' ')[-1]
    size = line[1].split(' ')[0]
    return int(vma, 16), sec, int(size, 16)

# return vma, file offset, size
def get_sec_info(sec_hdr, sec_name):
    lines = sec_hdr.split('\n')
    # skip first null section.
    sec_count = int(lines[0].split(' ')[2]) - 1
    lines = lines[5:5 + sec_count]
    for line in lines:
        line = [x for x in line[7:].split(' ') if x != '']
        if line[0] == sec_name:
            return tuple([int(x, 16) for x in line[2:5]])
    raise Exception('sec {} not found'.format(sec_name))

def vma_to_fo(sec_hdr, sec_name, vma):
    sec_vma, fo, size = get_sec_info(sec_hdr, sec_name)
    if vma - sec_vma > size or vma < sec_vma:
        raise Exception('vma {} out of range for sec {}'.format(vma, sec_name))
    return vma - sec_vma + fo

def flags_to_str(flags):
    s = []
    for bit, val in enumerate(VER_FLAGS):
        if flags & 2**bit:
            s.append(val)
    return ' '.join(s)

def dump_version_v1(path):
    try:
        elf_hdr = get_elf_header(path)
    except:
        print('Cannot read elf header for \'{}\'\n'.format(path))
        return

    try:
        sec_hdr = get_sec_header(path)
    except:
        print('Cannot read section header for \'{}\'\n'.format(path))
        return

    if get_type(elf_hdr) == 'EXEC':
        sym_name = '.hidden ' + BEEPVER_1_VER_SYMBOL
    else:
        sym_name = BEEPVER_1_VER_SYMBOL

    if get_ptr_size(elf_hdr) == 4:
        ptr_code = 'I'
    else:
        ptr_code = 'Q'

    if get_endian(elf_hdr) == 'little':
        endian_code = '<'
    else:
        endian_code = '>'

    try:
        ver_vma, ver_sec, ver_size = get_sym_info(path, sym_name)
    except:
        print('Cannot find version symbol for \'{}\'\n'.format(path))
        return

    ver_fo = vma_to_fo(sec_hdr, ver_sec, ver_vma)
    #ro_vma, ro_fo, ro_size = get_sec_info(sec_hdr, STR_SECTION)

    fmt = BEEPVER_1_VER_STRUCT.format(endian=endian_code, ptr=ptr_code)

    f = open(path, 'rb')
    f.seek(ver_fo)
    ver = struct.unpack(fmt, f.read(ver_size))

    print('Version info for \'{}\''.format(path))
    for val, (name, isstr) in zip(ver, BEEPVER_1_VER_MEMBERS):
        if isstr == True:
            f.seek(vma_to_fo(sec_hdr, STR_SECTION, val))
            val = f.read(STR_MAX)
            if val.find('\x00') != -1:
                val = val[:val.find('\x00')]

        if name == 'flags':
            val = '{:#x} {}'.format(val, flags_to_str(val))

        if type(val) == type(int()):
            val = '{:#x}'.format(val)

        print('  {:<10s}{}'.format(name + ':', val))
    f.close()
    print('')

def dump_version(path):
    try:
        sec_hdr = get_sec_header(path)
    except:
        print('Cannot read section header for \'{}\'\n'.format(path))
        return

    _, ver_fo, _ = get_sec_info(sec_hdr, '.beep')

    fmt = BEEPVER_2_VER_STRUCT
    f = open(path, 'rb')
    f.seek(ver_fo)
    ver = struct.unpack(fmt, f.read(struct.calcsize(fmt)))
    f.close()

    # The order and name of structure members are the same between v1 and v2.
    # In v1 they are stored in the string table and v2 they are directly in
    # the structure.  This limits us to a known size but it much easier to
    # read and later strip for binary diffs.
    print('Version info for \'{}\''.format(path))
    for val, (name, isstr) in zip(ver, BEEPVER_2_VER_MEMBERS):
        if isstr == True:
            # Remove trailing NULL chars.
            val = val.strip('\x00')

        if name == 'flags':
            val = '{:#x} {}'.format(val, flags_to_str(val))

        if type(val) == type(int()):
            val = '{:#x}'.format(val)

        print('  {:<10s}{}'.format(name + ':', val))
    print('')

def main(argv):
    parser = argparse.ArgumentParser(prog='pg_backup',
            formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--v1', action='store_true',
            help='use version 1 embedded versions')
    parser.add_argument('binary', type=str, nargs='+',
            help='path to binary')

    args = parser.parse_args(argv)

    for path in args.binary:
        if args.v1:
            dump_version_v1(path)
        else:
            dump_version(path)

if __name__ == '__main__':
    main(sys.argv[1:])
