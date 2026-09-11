#!/usr/bin/env python

import os
import sys
import beep.tekcsv
import matplotlib.pyplot as plt


##### helpers

def get_tek_data(path, glitch=False):
    ch1_index = 2
    ch2_index = 4

    if glitch is True:
        ch1_index = 1
        ch2_index = 3

    tekdata = beep.tekcsv.Tekcsv(path)
    ch_names = tekdata.channel_names()
    data = []
    for x in tekdata:
        data.append(x)
    data = map(list, zip(*data))
    data = [data[0], data[ch1_index], data[ch2_index]]
    sample_rate = 1/float(tekdata.ch_info[0]['Sample Interval'])
    print('data sampled at: {} S/s'.format(sample_rate))
    return data, ch_names[ch1_index - 1], ch_names[ch2_index - 1], sample_rate


def below_filter(values, threshold):
    return [val < threshold for val in values]

def find_ups(values, up_threshold, down_threshold):
    is_up = False
    up_count = 0
    down_count = 0
    result = []
    for val in values:
        if val:
            down_count = 0
            up_count += 1
        else:
            up_count = 0
            down_count += 1

        if up_count >= up_threshold:
            is_up = True

        if down_count >= down_threshold and is_up:
            is_up = False
            result.append(1)
        else:
            result.append(0)

    return result


def find_clicks(blue_vals, red_vals):
    turn_state = 0
    direction = 0

    result = []
    for blue, red in zip(blue_vals, red_vals):
        if not blue and not red:
            result.append(0)
        else:
            cur_dir = 0
            if blue:
                cur_dir = 1
            else:
                cur_dir = -1

            if turn_state < 3:
                # if turn_state is even we advance if cur_dir is equal to
                # the direction we think we're going. If it's odd we advance
                # in the opposite case
                if (((turn_state % 2) and cur_dir != direction)
                        or (not (turn_state % 2) and cur_dir == direction)):
                    turn_state += 1
                else:
                    direction = cur_dir
                    turn_state = 1
                result.append(0)
            else:
                if direction != cur_dir:
                    result.append(direction)
                else:
                    result.append(0)
                turn_state = 0
                direction = 0
    return result


def convert_to_adc(data, adc_prescalar, adc_sample_time, tek_sample_rate):
    adc_clk = 16e6
    adc_depth = 8
    adc_voltage = 2.5

    if adc_prescalar not in [1,2]:
        raise Exception('invalid prescalar value \'{}\''.format(adc_prescalar))

    if adc_sample_time not in [4,9,16,24,48,96,192,384]:
        raise Exception('invalid adc sample time \'{}\''.format(
                adc_sample_time))

    real_adc_sample_rate = (adc_clk / adc_prescalar /
            (adc_sample_time + adc_depth))

    target_over_sample_rate = tek_sample_rate / real_adc_sample_rate
    actual_over_sample_rate = int(round(target_over_sample_rate))
    sim_sample_rate = tek_sample_rate / actual_over_sample_rate
    sim_sample_period = 1 / sim_sample_rate

    #print adc_prescalar, adc_sample_time, path
    #print real_adc_sample_rate, tek_sample_rate, target_over_sample_rate

    print('adc sample rate: {} S/s'.format(real_adc_sample_rate))
    print('target over sample rate: {}'.format(target_over_sample_rate))
    if target_over_sample_rate < 2:
        print('Error: over sample rate is too low')
        sys.exit(2)

    print('sim data sample rate: {}'.format(sim_sample_rate))
    print('time distortion: {}%'.format(round((target_over_sample_rate -
            actual_over_sample_rate) / actual_over_sample_rate * 100, 2)))

    new_data = [list() for x in xrange(len(data))]
    last_point = (len(data[0]) / actual_over_sample_rate) * \
            actual_over_sample_rate

    min_val = 0
    max_val = (2**adc_depth) - 1

    for x in xrange(0, last_point, actual_over_sample_rate):
        new_data[0].append(sim_sample_period * x)
        for y in xrange(1, len(data)):
            avg = sum(data[y][x:x + actual_over_sample_rate]) / \
                    actual_over_sample_rate
            val = int(avg / adc_voltage * max_val)
            val = min(val, max_val)
            val = max(val, min_val)
            new_data[y].append(val)

    return max_val, new_data


##### top-level commands

# our current algorithm for detection knob rotation
def algo(args):
    glitch = False

    if args[2] == '--glitch':
        glitch = True
        path = args[3]
    else:
        path = args[2]

    data, ch1_name, ch2_name, tek_sample_rate = get_tek_data(path, glitch)
    print('Plotting \'{}\' (red), \'{}\' (blue)'.format(ch1_name, ch2_name))

    max_val, data = convert_to_adc(data, 2, 384, tek_sample_rate)

    below1 = below_filter(data[1], 128)
    below2 = below_filter(data[2], 128)

    ups1 = find_ups(below1, 5, 5)
    ups2 = find_ups(below2, 5, 5)

    clicks = find_clicks(ups1, ups2)

    fig = plt.figure()
    ax = fig.add_subplot(111)

    ax.plot(data[0], below1, 'r.')
    ax.plot(data[0], below2, 'b.')

    ax.plot(data[0], [v*2 for v in ups1], 'r.')
    ax.plot(data[0], [v*2 for v in ups2], 'b.')

    ax.plot(data[0], [int(v == -1)*3 for v in clicks], 'r.')
    ax.plot(data[0], [int(v == 1)*3 for v in clicks], 'b.')

    ax.set_ylim(-5, 5)
    plt.show()


def tekplot(args):
    glitch = False

    if args[2] == '--glitch':
        glitch = True
        path = args[3]
    else:
        path = args[2]

    data, ch1_name, ch2_name, _ = get_tek_data(path, glitch)
    print('Plotting \'{}\' (red), \'{}\' (blue)'.format(ch1_name, ch2_name))

    fig = plt.figure()
    ax = fig.add_subplot(111)
    ax.plot(data[0], data[1], 'r')
    ax.plot(data[0], data[2], 'b')
    ax.set_ylim(0, 5)
    plt.show()

def convert(args):
    glitch = False
    arg_start = 2

    if args[2] == '--glitch':
        glitch = True
        arg_start = 3

    adc_prescalar = int(args[arg_start])
    adc_sample_time = int(args[arg_start + 1])
    path = args[arg_start + 2]

    data, ch1_name, ch2_name, tek_sample_rate = get_tek_data(path, glitch)

    max_val, new_data = convert_to_adc(
            data, adc_prescalar, adc_sample_time, tek_sample_rate)

    fig = plt.figure()
    ax = fig.add_subplot(111)
    ax.plot(new_data[0], new_data[1], 'r')
    ax.plot(new_data[0], new_data[2], 'b')
    ax.set_ylim(0, max_val)
    plt.show()

def stplot(args):
    pass

if __name__ == '__main__':
    if sys.argv[1] == 'tekplot':
        tekplot(sys.argv)
    elif sys.argv[1] == 'convert':
        convert(sys.argv)
    elif sys.argv[1] == 'stplot':
        stplot(sys.argv)
    elif sys.argv[1] == 'algo':
        algo(sys.argv)
    else:
        print('invalid command')
        sys.exit(2)
