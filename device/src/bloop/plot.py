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

def main(argv):
    runs = json.loads(open(argv[2]).read())
    data = []
    for run in runs:
        jobs = {}
        for job in run:
            jobs[job['command']] = job
        print run
        data.append(jobs)

    if argv[1] == 'udp_sleep':
        plot_udp_sleep(data)
    elif argv[1] == 'udp_length':
        #plot_udp_length(data)
        pass


if __name__ == '__main__':
    main(sys.argv)
