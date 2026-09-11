#!/usr/bin/python

# used for figuring out what commands pandora sends.

import json
import re
import sys

cur_log = []

def process_line(line):
    global cur_log
    if 'LOGALL-START' in line:
        cur_log = []
    if 'LOGALL:' in line:
        line = ' '.join(line.split()[7:])#[:-4]
        cur_log.append(line)
    if 'LOGALL-END' in line:
        print
        message = ''.join(cur_log)
        match = re.match('Received a message on bus\((.*?)\) : (.*)', message)
        bus, json_message = match.groups()
        print bus, json_message
        m = json.loads(json_message)
        print bus, json.dumps(m['data'])

def main(argv):
    for line in open(argv[1]):
        if 'LOGALL' in line:
            process_line(line)

if __name__ == '__main__':
    main(sys.argv)
