#!/usr/bin/env python

import sys, socket, code

def send(s,namespace,msg,header=None):
    frame =  "Content-Length: %d\n" % (len(msg),)
    frame += "Namespace: %s\n" % (namespace,)
    if header:
        for k,v in header.iteritems():
            frame += "%s: %s\n" % (k, v)
    frame += "\n%s\n\n" % msg
    s.send(frame)

def connect(app, addr, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((addr, port))
    send(s,"__control__",
            "{\"type\":\"hello\",\"app\":\"%s\",\"user_agent\":\"commtest-py\"}" \
            % app)
    return s


def main(argv):
    global s
    app = "pandora"
    addr = "127.0.0.1"
    port = 32336
    if len(argv) < 2:
        print "Usage: %s <app> [host] [port]" % argv[0]
        print "  Default host:port is 127.0.0.1:32336"
        return
    else:
        app = argv[1].lower()

    if len(argv) < 3:
        print "Using default host 127.0.0.1"
    else:
        addr = argv[1]

    if len(argv) < 4:
        print "Using default port 32336"
    else:
        port = argv[2]

    s = connect(app, addr, port)
    print "Connection object is 's'"
    code.interact(local=globals())

s = None
if __name__ == '__main__':
    main(sys.argv)
