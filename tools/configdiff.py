#!/usr/bin/env python

import os
import sys
import commands

CMD = 'grep -vxFf {} {}'

def in_a_not_in_b(a, b):
    s, o = commands.getstatusoutput('grep -vxFf {} {}'.format(b, a))
    if (s == 256):
        return []
    elif (s != 0):
        print('error: grep exited with code {}'.format(s))
        print('message: {}'.format(o))
        sys.exit(2)
    return o.split('\n')

def config_keys(l):
    ret = []
    for s in l:
        beg = s.find('CONFIG_')
        end = [s.find(' ', beg), s.find('=', beg)]
        if beg == -1 or end == [-1, -1]:
            ret.append((None, s))
        else:
            ret.append((s[beg:min([x for x in end if x != -1])], s))
    return ret

def main(a, b):
    print('configdiff: {} {}'.format(a, b))
    old = config_keys(in_a_not_in_b(a, b))
    new = config_keys(in_a_not_in_b(b, a))
    if len(old) == 0 and len(new) == 0:
        print('file are the same')
        sys.exit(0)
    dnew = dict([x for x in new if x[0] != None])
    for k, v in old:
        print ('-{}'.format(v))
        if dnew.has_key(k):
            vnew = dnew.pop(k)
            print('+{}'.format(vnew))
    for k, v in new:
        if dnew.has_key(k) == True or k == None:
            print('+{}'.format(v))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('usage: {} <file> <file>'.format(sys.argv[0]))
        sys.exit(2)
    main(*sys.argv[1:])

