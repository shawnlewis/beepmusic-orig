#!/usr/bin/python

import socket
import sys
import time

UDP_IP = "192.168.0.102"
UDP_PORT = 5005

count = 0

def print_count():
    print count

import atexit
atexit.register(print_count)


def main(argv):
    global count
    sock = socket.socket(socket.AF_INET, # Internet
                         socket.SOCK_DGRAM) # UDP
    sock.bind((UDP_IP, UDP_PORT))

    while True:
        data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
        print count
        count += 1

if __name__ == '__main__':
    main(sys.argv)
