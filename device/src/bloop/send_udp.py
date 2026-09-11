#!/usr/bin/python

import socket
import time

UDP_IP = "192.168.0.102"
UDP_PORT = 5005
MESSAGE = "Hello, World!"

print "UDP target IP:", UDP_IP
print "UDP target port:", UDP_PORT
print "message:", MESSAGE


def send_udp(from_ip, to_ip, message, count, sleep_period):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((from_ip, 0))

    for i in xrange(count):
        sock.sendto(message, (to_ip, UDP_PORT))
        if sleep_period:
            time.sleep(sleep_period)
