#!/usr/bin/env python

### Shell setup (openwrt) ###
# cd /proc/<pid to watch>
# while :; do date; cat /proc/meminfo; grep Vm status; sleep 10; done;

import os
import sys
import matplotlib
matplotlib.use('Agg')
from matplotlib import pylab

ts = 10

def main(path):
    f = open(path, 'r')
    data = {}

    for line in f:
        if line.find('UTC') != -1 or line.find('kB') == -1:
            continue
        key, val = [x.strip() for x in line.strip().split(':')]
        if data.has_key(key) is False:
            data[key] = []
        data[key].append(val[:-3])
    t = range(0,len(data['MemTotal'])*ts,ts)
    pylab.plot(t, data['MemFree'], label='MemFree')
    pylab.plot(t, data['Active'], label='Active')
    pylab.plot(t, data['Inactive'], label='Inactive')
    pylab.plot(t, data['VmData'], label='VmData')
    pylab.plot(t, data['Cached'], label='Cached')
    pylab.plot(t, data['VmRSS'], label='VmRSS')
    pylab.plot(t, data['VmHWM'], label='VmHWM')
    pylab.legend(loc='upper center', bbox_to_anchor=(0.5, 1.1), ncol=4)
    pylab.xlabel('t (s)')
    pylab.ylabel('mem stats (kb)')
    pylab.savefig(os.path.splitext(path)[0] + '.png')

if __name__ == '__main__':
    path = 'proc_trace.txt'
    if len(sys.argv) == 2:
        path = sys.argv[1]
    main(path)

