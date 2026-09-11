#!/usr/bin/python

import copy
import json
import signal
import spur
import sys
import time


class Error(Exception):
    pass


def run(shell, command, params):
    if shell == 'local':
        shell = spur.LocalShell()
    else:
        raise Error('Invalid shell %s' % shell)
    return shell.spawn(['./run.py', command, json.dumps(params)],
                       store_pid=True)


def run_jobs(jobs):
    jobs = copy.deepcopy(jobs)
    processes = []
    for job in reversed(jobs):
        processes.append(run(job['shell'], job['command'], job['params']))
        time.sleep(.5)
    for i, (process, job) in enumerate(zip(reversed(processes), jobs)):
        if i != 0:
            process.send_signal(signal.SIGTERM)
        result = process.wait_for_result()
        job.update({
            'return_code': result.return_code,
            'output': json.loads(result.output)})
        time.sleep(.5)
    return jobs

def sweep_sleep(receive_job, send_job):
    results = []
    total_time = 4
    for sleep in [0, .00001, .00005, .0001, .0005, .001, .005, .01, .05]:
        receive_job = copy.deepcopy(receive_job)
        send_job = copy.deepcopy(send_job)
        if sleep == 0:
            count = 400000
        else:
            count = int(total_time / sleep)
        if count > 40000:
            count = 40000
        send_job['params']['count'] = count
        send_job['params']['sleep_period'] = sleep
        result = run_jobs([send_job, receive_job])
        print result
        results.append(result)
    return results


def sweep_length(receive_job, send_job):
    results = []
    for length in [1, 10, 100, 200, 500, 800, 1000]:
        receive_job = copy.copy(receive_job)
        send_job = copy.copy(send_job)
        send_job['params']['message_len'] = length
        result = run_jobs([send_job, receive_job])
        results.append(result)
    return results


def save_results(fname, results):
    open(fname, 'w').write(json.dumps(results, indent=4))

def main(argv):
    send_job = {'shell': 'local',
                'command': 'send_udp',
                'params': {'bind_ip': '192.168.0.104',
                           'to_ip': '192.168.0.102',
                           'message_len': 100,
                           'count': 10000,
                           'sleep_period': .0001}}
    receive_job = {'shell': 'local',
                   'command': 'receive_udp',
                   'params': {'bind_ip': '192.168.0.102'}}
    results = []
    for i in xrange(10):
        results += sweep_sleep(receive_job, send_job)
    save_results('udp_sleep.txt', results)

    #results = []
    #for i in xrange(10):
    #    results += sweep_length(copy.copy(receive_job), copy.copy(send_job))
    #save_results('sweep_length.txt', results)


if __name__ == '__main__':
    main(sys.argv)
