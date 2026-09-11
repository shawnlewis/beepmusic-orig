#!/usr/bin/env python

### Javascript setup ###
# npm -g install memwatch
#
# put this somewhere in your js files.
#
#var memwatch = require('memwatch');
#
#memwatch.on('leak', function(info) {
#    console.log('in beep on leak');
#    console.log(info);
#});
#
#memwatch.on('stats', function(stats) {
#    console.log('in beep on stats');
#    console.log(JSON.stringify(hd.end(), null, "  "));
#    hd = new memwatch.HeapDiff();
#    console.log(stats);
#});

import os
import sys
import matplotlib
matplotlib.use('Agg')
from matplotlib import pylab

def get_after_size(path):
    f = open(path, 'r')
    hd_list = []
    t = None

    for line in f:
        if line.strip() == 'in beep on stats':
            t = ''
        elif t != None:
            if line[0] not in [' ', '}', '{']:
                continue
            t += line.strip()
            if line == '}\n':
                hd_list.append(eval(t))
                t = None
    after = []
    for hd in hd_list:
        after.append(int(hd['after']['size_bytes'])/1024)
    return after

def main(path_list):
    data = {}
    for path in path_list:
        data[path] = get_after_size(path)

    for key in data.keys():
        t = range(len(data[key]))
        pylab.plot(t, data[key], label=key)
    pylab.legend(loc='upper center', bbox_to_anchor=(0.5, 1.1), ncol=4)
    pylab.xlabel('gc_collections (songs played)')
    pylab.ylabel('js heap after gc (kb)')
    pylab.savefig('js_heap.png')

if __name__ == '__main__':
    path_list = [
        'js_heap_trace.txt',
        ]

    if len(sys.argv) >= 2:
        path_list = sys.argv[1:]
    main(path_list)

