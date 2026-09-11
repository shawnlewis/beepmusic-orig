#!/usr/bin/env python

import os
import sys
sys.path.insert(1, os.path.join(os.path.realpath(os.path.split(__file__)[0]), 'beep-packages'))

import argparse
import math
import textwrap

import beep.utils
import beep.nmt as nmt
from beep.utils import quick_cmd

try:
    import argcomplete
except:
    pass

debug = 0

def dprint(s, l=1):
    if debug >= l:
        print(s)

def annotate(args):
    t = nmt.NMTrace(args.inpath,
            caller_root_dir=args.root,
            caller_trim_dir=args.trim)
    for rec in t:
        print(t.record_annotate())
    t.close()

def leak_check(args):
    t = nmt.NMTrace(args.inpath,
            caller_root_dir=args.root,
            caller_trim_dir=args.trim)
    # populate the mmap.
    for rec in t:
        pass
    mmap = t.get_annotated_mmap(False)
    t.close()
    if len(mmap) == 0:
        print('No leaks detected')
        return

    for k in sorted(mmap.keys()):
        v = mmap[k]
        rtype = nmt.record_type_from_code(v[2])
        print('{} bytes at {:x} from {} at {}'.format(
                v[1], k, rtype, v[3]))

def heap_plot2(args):
    t = nmt.NMTrace(args.inpath,
            caller_root_dir=args.root,
            caller_trim_dir=args.trim)
    all_stats = t.get_stats()
    print(all_stats)
    split_addr = (all_stats[2] - all_stats[1]) / 2
    print(split_addr)
    print(t.get_filtered_stats(ignore_below=split_addr))
    print(t.get_filtered_stats(ignore_above=split_addr))

def heap_plot(args):
    # Don't load these unless we need them.
    import matplotlib.image as mpimg
    import matplotlib.pyplot as plt
    import numpy as np

    print('heap plot')
    t = nmt.NMTrace(args.inpath,
            caller_root_dir=args.root,
            caller_trim_dir=args.trim)
    t_len, tot_addr_min, tot_addr_max = t.get_stats()

    print(t_len, tot_addr_min, tot_addr_max)

    if args.div == 1:
        addr_min = [tot_addr_min]
        addr_max = [tot_addr_max]
    else:
        split_d = (tot_addr_max - tot_addr_min) / args.div
        addr_ig_b = range(tot_addr_min, tot_addr_max, split_d)
        addr_ig_a = addr_ig_b[1:]
        addr_ig_b[0] = 0
        addr_ig_a.append(2**64 - 1)

        addr_min = []
        addr_max = []
        # Get the number of lines that fall into each range for a more accurate
        # range and to eliminate any empty ranges.
        for ig_b, ig_a in zip(addr_ig_b, addr_ig_a):
            print('')
            print(ig_b, ig_a)
            f_stats = t.get_filtered_stats(ignore_below=ig_b,
                    ignore_above=ig_a)
            print(f_stats)
            if f_stats[0]:
                addr_min.append(f_stats[1])
                addr_max.append(f_stats[2])

    if t_len <= args.x:
        xd = 1
        xlen = t_len
    else:
        xd = t_len / args.x
        xlen = int(math.ceil(float(t_len)/xd))

    yd = []
    ylen = []
    data = []
    data_count = len(addr_min)

    print(t_len, addr_min, addr_max, 0)
    print(xd,xlen)
    print(yd,ylen)

    #for amin, amax in zip(addr_min, addr_max):
    for i in xrange(data_count):
        addr_len = addr_max[i] - addr_min[i]
        print addr_len
        if addr_len <= args.y:
            yd.append(1)
            ylen.append(addr_len)
        else:
            yd.append(addr_len / args.y)
            ylen.append(int(math.ceil(float(addr_len)/yd[i])))
        # Get the real range of the heap byte buckets.
        addr_max[i] = addr_min[i] + (yd[i] * ylen[i])

        # Create the numpy arrays.
        data.append(np.ndarray((ylen[i],xlen,3), np.uint8))
        data[i].fill(255)

    print('')
    print(t_len, addr_min, addr_max, addr_len)
    print(xd,xlen)
    print(yd,ylen)

    print('')
    for x in xrange(xlen):
        sys.stdout.write('\x1b[2K\x1b[0G')
        sys.stdout.write('processing heap snapshot {}...'.format(x))
        sys.stdout.flush()
        arr = xd
        while arr:
            t.record_next()
            arr -= 1
            #print('advancing')
        mmap = t.get_mmap(make_copy=False)
        # Need to keep a rolling sum for allocs that spill between buckets.
        all_addrs = sorted(mmap.keys())
        for i in xrange(data_count):
            bsum = 0
            addrs = [j for j in all_addrs if j >= addr_min[i] and j < addr_max[i]]
            for addr in xrange(addr_min[i], addr_max[i], yd[i]):
                while len(addrs) and addrs[0] < (addr + yd[i]):
                    if mmap[addrs[0]][0] is True:
                        bsum += mmap[addrs[0]][1]
                    addrs = addrs[1:]
                if bsum:
                    y = ylen[i] - ((addr - addr_min[i]) / yd[i]) - 1
                    v = min(255, int(float(bsum)/yd[i] * 255))
                    data[i][y][x] = [v, 0, 0]
                    bsum -= min(bsum, yd[i])

    print('')

    for i in xrange(data_count):
        plt.figure()
        plt.imshow(data[i], interpolation='none')
    plt.show()

if __name__ == '__main__':
    cmds = {
        'annotate': annotate,
        'leak-check': leak_check,
        'heap-plot': heap_plot
    }

    parser = argparse.ArgumentParser(prog='nmt',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog=textwrap.dedent('''\
            Default values:
                ROOT: {}
                TRIM: {}
            '''.format(beep.utils.root_dir(),
                    beep.utils.build_dir())))
    parser.add_argument('-d', '--debug', type=int, choices=(0,1,2))
    parser.add_argument('--root', type=str, help='root dir for caller resolution',
            default=beep.utils.root_dir())
    parser.add_argument('--trim', type=str, help='dir to trim for resolved caller',
            default=beep.utils.build_dir())
    subparsers = parser.add_subparsers(help='sub-command help', dest='cmd',
            title='Commands', metavar='<command>')

    annotate_parser = subparsers.add_parser('annotate', help='')
    annotate_parser.add_argument('inpath', type=str, metavar='path')

    leak_check_parser = subparsers.add_parser('leak-check', help='')
    leak_check_parser.add_argument('inpath', type=str, metavar='path')

    heap_plot_parser = subparsers.add_parser('heap-plot', help='')
    heap_plot_parser.add_argument('inpath', type=str, metavar='path')
    heap_plot_parser.add_argument('--x', type=int, default=100)
    heap_plot_parser.add_argument('--y', type=int, default=100)
    heap_plot_parser.add_argument('--div', type=int, default=1)

    if 'argcomplete' in sys.modules.keys():
        argcomplete.autocomplete(parser)

    args = parser.parse_args()

    args.root = os.path.realpath(args.root)
    args.trim = os.path.realpath(args.trim)
    if args.trim[-1] != '/':
        args.trim += '/'

    debug = args.debug
    dprint(args)

    cmds[args.cmd](args)
