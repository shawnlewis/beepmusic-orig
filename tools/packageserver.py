#!/usr/bin/python

import BaseHTTPServer
import SimpleHTTPServer

def _bare_address_string(self):
    host, port = self.client_address[:2]
    return str(host)

BaseHTTPServer.BaseHTTPRequestHandler.address_string = \
        _bare_address_string

if __name__ == '__main__':
    SimpleHTTPServer.test()
