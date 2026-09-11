#!/usr/bin/python

import random
import socket
import sys
import time


# choose random start level, send bloops to start
# send N-bit message:
#    each bit is a group of L levels
#    each message is DxC data by code words

def send_level(sock, level):
    message = 'x' * level
    sock.sendto(message, ('192.168.0.254', 5005))
    time.sleep(.001)

def send_start(sock, start_level):
    for rep in xrange(20):
        for i in xrange(5):
            send_level(sock, start_level + i)

def send_bit(sock, bit, start_level, offset):
    #print start_level, offset
    assert bit == 0 or bit == 1
    for i in xrange(3):
        level = start_level + offset * 6 + bit * 3 + i
        #print i, level
        send_level(sock, level)

def send_byte(sock, byte, start_level, offset):
    for i in xrange(8):
        #print '\t', i
        send_bit(sock, byte >> i & 1, start_level, offset * 8 + i)

def send_message(sock, message):
    start_level = random.randrange(1, 398)
    start_level = 10
    print 'START_LEVEL', start_level
    send_start(sock, start_level)

    for i in xrange(20):
        for i, c in enumerate(message[:8]):
            send_byte(sock, ord(c), start_level+10, i)

    time.sleep(.25)
    for i in xrange(5):
        send_start(sock, start_level+5)

def main(argv):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('192.168.0.111',0))

    send_message(sock, argv[1][:8])

    #count = 0
    #while 1:
    #    send_message(sock, 'shawn')
    #    #for i in xrange(3):

    #    #    send(sock, i)
    #    #print count
    #    #count += 1

if __name__ == '__main__':
    main(sys.argv)
