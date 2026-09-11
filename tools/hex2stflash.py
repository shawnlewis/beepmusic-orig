#!/usr/bin/env python

import os
import sys
sys.path.insert(1, os.path.join(os.path.realpath(os.path.split(__file__)[0]), 'beep-packages'))

import argparse
import md5
import pickle
import textwrap

try:
    import argcomplete
except:
    pass

debug = 0

def dprintf(s, l=1):
    if debug >= l:
        print(s)

def chksum_hex_line(line):
    chksum = 0
    for x in xrange(1, len(line), 2):
        val = int(line[x:x + 2], 16)
        chksum += val
    if (chksum & 0xff) != 0:
        raise Exception('invalid checksum for line {}...'.format(line[:9]))

def hex_str_to_str(s):
    blob = ''
    for x in xrange(0, len(s), 2):
        blob += chr(int(s[x:x + 2], 16))
    return blob

def str_to_hex_str(s):
    fmt = ''.join(['{:02x}',] * len(s))
    o = [ord(c) for c in s]
    return fmt.format(*o)

def read_hex_file(path):
    f = open(path, 'r')
    base_addr = None
    next_addr = None
    blob = ''
    for line in f:
        line = line.strip()
        chksum_hex_line(line)
        size = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rec_type = int(line[7:9], 16)

        if rec_type == 0:
            data = hex_str_to_str(line[9:-2])

            if base_addr is None:
                base_addr = addr
                next_addr = addr

            if next_addr != addr:
                raise Exception('address holes in hex files not supported')

            blob += data
            next_addr += size
        elif rec_type == 1:
            f.close()
            if base_addr is None or len(blob) == 0:
                raise Exception('no data in hex file')
            return base_addr, blob
    f.close()
    raise Exception('hex file EOF not found')

def st_chksum(s):
    chksum = 0
    for x in s:
        chksum = chksum ^ ord(x)
    return chr(chksum)

def st_compliment(c):
    return chr(0xff ^ ord(c))

ST_CMD = {
    'GET':      '\x00\xff',
    'READ':     '\x11\xee',
    'ERASE':    '\x43\xbc',
    'WRITE':    '\x31\xce',
    'GO':       '\x21\xde'
}

ST_CODE = {
    'SYNCH':    '\x7f',
    'ACK':      '\x79',
    'NACK':     '\x1f',
    'BUSY':     '\xaa'
}

ST_PAGE_SIZE = 0x80
ST_BOOT_ADDR = 0x8000

def st_scr_sync():
    return [(ST_CODE['SYNCH'], ST_CODE['ACK'], 2)]

def st_scr_bootloader_info():
    return [
        (
            ST_CMD['GET'],
            '\x79\x05\x12\x00\x11\x21\x31\x43\x79',
            2
        )]

def st_scr_erase_flash():
    return [
        (ST_CMD['ERASE'], ST_CODE['ACK'], 2),
        ('\xff\x00', ST_CODE['ACK'], 5)]

def st_split_data_to_pages(addr, data):
    if len(data) == 0:
        raise Exception('empty data')

    ret = []
    page_mask = ST_PAGE_SIZE - 1
    write_addr = addr
    while(len(data)):
        if write_addr & page_mask != 0:
            size = ((write_addr + page_mask) & ~page_mask) - write_addr
        else:
            size = ST_PAGE_SIZE

        write_data = data[:size]
        data = data[size:]

        ret.append((write_addr, write_data))
        write_addr += size

    return ret

def st_scr_write_mem(addr, data):
    scr = []
    md5sum = md5.new()
    for page_addr, page_data in st_split_data_to_pages(addr, data):
        md5sum.update(page_data)
        page_addr_str = hex_str_to_str('{:08x}'.format(page_addr))
        page_addr_str += st_chksum(page_addr_str)

        # Write size is N - 1 bytes.
        page_data = chr(len(page_data) - 1) + page_data
        page_data += st_chksum(page_data)

        scr.append((ST_CMD['WRITE'], ST_CODE['ACK'], 2))
        scr.append((page_addr_str, ST_CODE['ACK'], 2))
        scr.append((page_data, ST_CODE['ACK'], 2))

    return scr

def st_scr_verify_mem(addr, data):
    scr = []
    for page_addr, page_data in st_split_data_to_pages(addr, data):
        page_addr_str = hex_str_to_str('{:08x}'.format(page_addr))
        page_addr_str += st_chksum(page_addr_str)

        # Read size is N - 1 bytes.
        size_str = chr(len(page_data) - 1)
        size_str += st_compliment(size_str)

        page_data = ST_CODE['ACK'] + page_data

        scr.append((ST_CMD['READ'], ST_CODE['ACK'], 2))
        scr.append((page_addr_str, ST_CODE['ACK'], 2))
        scr.append((size_str, page_data, 2))

    return scr

def st_scr_go(addr):
    addr_str = hex_str_to_str('{:08x}'.format(addr))
    addr_str += st_chksum(addr_str)
    return [
        (ST_CMD['GO'], ST_CODE['ACK'], 2),
        (addr_str, ST_CODE['ACK'], 2)]

def main(args):
    scr = []
    scr += st_scr_sync()
    scr += st_scr_bootloader_info()
    for inpath in args.inpath:
        if inpath == 'ERASE_FLASH':
            scr += st_scr_erase_flash()
        else:
            addr, data = read_hex_file(inpath)
            scr += st_scr_write_mem(addr, data)
            scr += st_scr_verify_mem(addr, data)
            md5sum = md5.new()
            md5sum.update(data)
            print('Added file {}\n  md5sum {}'.format(inpath,
                    md5sum.hexdigest()))
    scr += st_scr_go(ST_BOOT_ADDR)

    outfile = open(args.outpath, 'w')
    pickle.dump(scr, outfile)
    outfile.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(prog='hex2stflash',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description='Hex file to stm8l flashing script utility',
            epilog=textwrap.dedent('''\
            Common usage for mass production scripts:
            hex2stflash \\
                stm8l_disable_write_protect.hex \\
                bootloader_NNNN_BBBB.hex \\
                controller_eeprom_NNNN_CCCC.hex \\
                controller_NNNN_CCCC.hex \\
                stm8l_enable_write_protect.hex \\
                stm8l_full_NNNN_BBBB_CCCC.scr
            '''))
    parser.add_argument('-d', '--debug', type=int, choices=(0,1,2), default=0)
    parser.add_argument('inpath', type=str, metavar='input', nargs='+',
            help='hex file input or ERASE_FLASH')
    parser.add_argument('outpath', type=str, metavar='output',
            help='script output path')

    if 'argcomplete' in sys.modules.keys():
        argcomplete.autocomplete(parser)

    args = parser.parse_args()

    debug = args.debug
    main(args)
