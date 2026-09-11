#!/usr/bin/env python

### GDB setup ###
# Breakpoint 1: b mmap
# Breakpoint 2: b (*mmap + 67)
# Breakpoint 3: b munmap
#
#(gdb) info b
#Num     Type           Disp Enb Address    What
#1       breakpoint     keep y   0xb7da50e0 ../sysdeps/unix/sysv/linux/i386/mmap.S:35
#        stop only if *(unsigned int)($esp + 8) >= 1048576
#        breakpoint already hit 41 times
#        bt
#        p *(unsigned int)($esp + 8)
#        shell date +%s
#        enable 2
#        c
#2       breakpoint     keep n   0xb7da5123 ../sysdeps/unix/sysv/linux/i386/mmap.S:103
#        breakpoint already hit 40 times
#        disable 2
#        p/x $eax
#        c
#3       breakpoint     keep y   0xb7da51d0 ../sysdeps/unix/syscall-template.S:82
#        breakpoint already hit 64 times
#        p/x *(unsigned int)($esp + 4)
#        p *(unsigned int)($esp + 8)
#        shell date +%s
#        c

import os
import sys
import matplotlib
matplotlib.use('Agg')
from matplotlib import pylab

ALL_UNMAP = True

MAP = 1
MAP_RET = 2
UNMAP = 3

def parse_map(f):
    line = f.next()
    while line.find('$') != 0:
        line = f.next()
    size = int(line.split('=')[1].strip())
    time = int(f.next())

    line = f.next()
    while line.find('Breakpoint %d' % MAP_RET) != 0:
        line = f.next()
    while line.find('$') != 0:
        line = f.next()
    addr = int(line.split('=')[1].strip(), 16)
    return ('m', time, size, addr)

def parse_unmap(f):
    line = f.next()
    while line.find('$') != 0:
        line = f.next()
    addr = int(line.split('=')[1].strip(), 16)
    size = int(f.next().split('=')[1].strip())
    time = int(f.next())
    return ('u', time, size, addr)

def main(path):
    f = open(path, 'r')
    data = []

    for line in f:
        if line.find('Breakpoint %d' % MAP) == 0:
            # always track maps
            data.append(parse_map(f))
        elif line.find('Breakpoint %d' % UNMAP) == 0:
            if ALL_UNMAP == True:
                data.append(parse_unmap(f))
            else:
                # only track unmaps if there is a map for it's size and address
                umap = parse_unmap(f)
                matches = [x for x in data if x[3] == umap[3]]
                if len(matches) == 0:
                    continue
                elif matches[-1][0] != 'm':
                    continue
                else:
                    data.append(umap)
    if len(data) == 0:
        print 'no maps found'
        return
    t_start = data[0][1]
    x = [0]
    y = [data[0][2]]
    for e in data[1:]:
        mul = 1
        if e[0] == 'u':
            mul = -1
        x.append(e[1] - t_start)
        y.append(y[-1] + (e[2] * mul))
    y = [i/1024 for i in y]

    pylab.plot(x, y, label='vm maps')
    pylab.legend(loc='upper center', bbox_to_anchor=(0.5, 1.1), ncol=4)
    pylab.xlabel('t (s)')
    pylab.ylabel('mem mapped (kb)')
    pylab.savefig(os.path.splitext(path)[0] + '.png')


if __name__ == '__main__':
    path = 'mmap_trace.txt'
    if len(sys.argv) == 2:
        path = sys.argv[1]
    main(path)

