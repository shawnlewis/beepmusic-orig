#!/usr/bin/env python

import json
import sys

from matplotlib import pyplot


def plot_udp_sleep(data):
    # plot received_ratio v sleep time
    ratios = []
    sleep_times = []
    for d in data:
        ratios.append(d['receive_udp']['output']['count'] /
                      float(d['send_udp']['params']['count']))
        if ratios[-1] > 1:
            print d
        sleep_time = d['send_udp']['params']['sleep_period']
        if sleep_time == 0:
            sleep_time = .000001
        sleep_times.append(sleep_time)
    print ratios
    print sleep_times
    #pyplot.subplot(2, 1, 1)
    pyplot.plot(sleep_times, ratios, '.')
    pyplot.xscale('log')
    pyplot.xlim([0.0000005, 1])
    pyplot.ylim([0, 1.1])
    pyplot.show()

def plot_data(data):
    plot_input = []
    for d in data.values():
        plot_input += zip(*d)
    pyplot.plot(*plot_input)
    pyplot.show()

def burn_data(logname, for_test):
    data = {}
    for line in open(logname):
        fields = line.split()
        if len(fields) != 11 or 'DISK' in line:
            continue
        host = fields[3].split('.')[0]
        test_name = fields[5]
        try:
            time = float(fields[6])
        except ValueError:
            continue
        try:
            rate = int(fields[10])
        except ValueError:
            continue
        if (test_name == for_test):
            data.setdefault(host, []).append((time, rate))
    return data


def main(argv):
    data = burn_data(argv[1], 'NETWORK_WRITE')
    plot_data(data)


if __name__ == '__main__':
    main(sys.argv)
