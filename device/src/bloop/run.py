#!/usr/bin/python

UDP_PORT = 5005

import json
import signal
import socket
import sys
import time


result = {}

def print_result(signum, frame):
    print json.dumps(result)
    exit(0)
signal.signal(signal.SIGTERM, print_result)

def send_udp(bind_ip, to_ip, message_len, count, sleep_period):
    message = 'x' * message_len

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_ip, 0))

    for i in xrange(count):
        sock.sendto(message, (to_ip, UDP_PORT))
        if sleep_period:
            time.sleep(sleep_period)
    print_result(None, None)


def receive_udp(bind_ip):
    global result

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_ip, UDP_PORT))

    result['count'] = 0
    while True:
        data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
        result['count'] += 1

def coerce_string(s):
    val = s
    try:
        val = int(s)
    except ValueError:
        try:
            val = float(s)
        except ValueError:
            pass
    return val

def main(argv):
    func = globals()[argv[1]]
    args = json.loads(''.join(argv[2:]))
    print json.dumps(func(**args))


if __name__ == '__main__':
    main(sys.argv)
