#!/usr/bin/python

import socket
import sys
import time

BASE = 100
CARRIER_MULT = 10
SIGNAL_MULT = 11

VALS = {
    0: (6, 7, 8, 6, 7, 8, 6, 7, 8),
    1: (9, 10, 11, 9, 10, 11, 9, 10, 11),
    2: (12, 13, 14, 12, 13, 14, 12, 13, 14)}

MESSAGE_BASE = 'x' * BASE
MESSAGE_CARRIER_MULT = 'x' * CARRIER_MULT
MESSAGE_SIGNAL_MULT = 'x' * SIGNAL_MULT


def send_carrier_level(sock, level):
    for i in xrange(5):
        message = MESSAGE_BASE + MESSAGE_CARRIER_MULT * level
        sock.sendto(message, ('192.168.0.254', 5005))
        time.sleep(.001)

def send_signal_level(sock, level):
    for i in xrange(5):
        message = MESSAGE_BASE + MESSAGE_SIGNAL_MULT * level
        sock.sendto(message, ('192.168.0.254', 5005))
        time.sleep(.001)

count = 0
def send(sock, val):
    global count
    count += 1
    print count
    assert 0 <= val and val <= 2
    for i in xrange(1, 4):
        send_carrier_level(sock, i)
    for i in VALS[val]:
        send_signal_level(sock, i)
    for i in xrange(4, 6):
        send_carrier_level(sock, i)
    #time.sleep(.01)

def send_bit(sock, bit):
    assert bit == 0 or bit == 1
    print '\t\t',
    for i in xrange(5):
        print 2,
        send(sock, 2)
    for i in xrange(5):
        send(sock, bit)
        print bit,
    print

def send_byte(sock, byte):
    for i in xrange(8):
        print '\t', i
        send_bit(sock, byte >> i & 1)

def send_message(sock, message):
    for char in message:
        print char
        val = ord(char)
        send_byte(sock, val)

def main(argv):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('192.168.0.106',0))

    count = 0
    while 1:
        send_message(sock, 'shawn')
        #for i in xrange(3):

        #    send(sock, i)
        #print count
        #count += 1

if __name__ == '__main__':
    main(sys.argv)
