#!/usr/bin/python

import os
import sys
import operator
try:
    import gdb
except:
    pass

BEEP_BINS = [
    'ubusd',
    'playnet',
    'app_webradio',
    'urelay',
    'beepdiscovery',
    'uhttpd',
    'app_pandora',
    'beepdevio',
    'beepjs',
    'beepalsa'
]

BEEP_LUAS = [
    'beepmanager',
    'distributor',
    'beephead',
    'beephealth'
]


def get_all_pids():
    ret = []
    pids = [pid for pid in os.listdir('/proc') if pid.isdigit()]
    for pid in pids:
        cmd = open(os.path.join('/proc', pid, 'cmdline'), 'r').read()
        cmd = [x for x in cmd.split('\0') if x != '']
        # Empty ones are kernel threads.
        if len(cmd) > 0:
            cmd[0] = os.path.split(cmd[0])[1]
            ret.append([cmd, pid])
    return ret

def get_devname(cmd):
    index = -1
    if cmd[0] == 'ubusd' and '-s' in cmd:
        index = cmd.index('-s') + 1
        if len(cmd) < index:
            return 'unknown'
    elif cmd[0] == 'uhttpd' and '-U' in cmd:
        index = cmd.index('-U') + 1
        if len(cmd) < index:
            return 'unknown'
    else:
        index = [x[0] for x in enumerate(cmd) if x[1].find('--ubus') == 0]
        if len(index) == 0:
            return 'unknown'
        index = index[0]

    dev = os.path.split(cmd[index])[1].split('.')
    if dev[1] != 'ubus':
        return 'unknown'
    return dev[0]

def get_beep_pids():
    ret = []
    all_pids = get_all_pids()
    for cmd, pid in all_pids:
        full_cmd = ' '.join(cmd)
        if cmd[0] == 'lua':
            for lua in BEEP_LUAS:
                if full_cmd.find(lua) != -1:
                    ret.append([get_devname(cmd), lua, pid])
                    break
        elif cmd[0] in BEEP_BINS:
            ret.append([get_devname(cmd), cmd[0], pid])
    ret.sort(key=operator.itemgetter(0,1))
    return ret

def xxd_dump(addr, data):
    offset = 0
    for x in range(0, len(data), 16):
        hex_line = []
        for p in range(8):
            twobytes = data[x + (2 * p):x + (2 * p) + 2]
            if len(twobytes) == 0:
                break
            twobytes = [ord(c) for c in twobytes]
            fmt = ''.join(['{:02x}',] * len(twobytes))
            hex_line.append(fmt.format(*twobytes))
        hex_line = ' '.join(hex_line)

        str_line = ''
        for b in data[x:x + 16]:
            o = ord(b)
            if o >= 0x20 and o <= 0x7e:
                str_line += chr(o)
            else:
                str_line += '.'
        print('0x{:04x}: {:<39} {}'.format(offset, hex_line, str_line))
        offset += 16

try:
    class BeepGdbCmd(gdb.Command):
        def __init__(self):
            super(BeepGdbCmd, self).__init__('beep',
                    gdb.COMMAND_NONE, gdb.COMPLETE_NONE, True)

        def invoke(self, args, from_tty):
            gdb.execute('help beep')

    class BeepGdbCmdShow(gdb.Command):
        def __init__(self):
            super(BeepGdbCmdShow, self).__init__('beep show',
                    gdb.COMMAND_NONE, gdb.COMPLETE_NONE, True)

        def invoke(self, args, from_tty):
            gdb.execute('help beep show')

    class BeepGdbCmdShowPids(gdb.Command):
        def __init__(self):
            super(BeepGdbCmdShowPids, self).__init__('beep show pids',
                    gdb.COMMAND_NONE, gdb.COMPLETE_NONE, True)

        def invoke(self, args, from_tty):
            fmt = '{:<10}{:<20}{:<10}'
            print(fmt.format('device', 'cmd', 'pid'))
            for dev, cmd, pid in get_beep_pids():
                print(fmt.format(dev, cmd, pid))

    class BeepGdbCmdAttach(gdb.Command):
        def __init__(self):
            super(BeepGdbCmdAttach, self).__init__('beep attach',
                    gdb.COMMAND_NONE)

        def complete(self, text, word):
            beep_pids = get_beep_pids()
            i = len(text.split(' ')) - 1
            if i == 1:
                beep_pids = [x for x in beep_pids if x[0]
                        == text.split(' ')[0]]
            elif i > 1:
                return None
            params = list(set([x[i] for x in beep_pids
                    if x[i].startswith(word)]))
            return params

        def invoke(self, args, from_tty):
            argv = gdb.string_to_argv(args)
            if len(argv) != 2:
                gdb.execute('help beep attach')
                return
            pid = [x for x in get_beep_pids() if x[0] == argv[0] and
                    x[1] == argv[1]]
            if len(pid) == 0:
                print('Cannot find pid for \'{}\''.format(args))
                return
            print('attaching to \'{}\' at pid {}'.format(args, pid[0][2]))
            gdb.execute('attach {}'.format(pid[0][2]))

    class BeepGdbCmdXxd(gdb.Command):
        def __init__(self):
            super(BeepGdbCmdXxd, self).__init__('beep xxd',
                    gdb.COMMAND_DATA)

        def invoke(self, args, from_tty):
            argv = gdb.string_to_argv(args)
            if len(argv) != 2:
                raise gdb.GdbError('xxd takes 2 arguments.')

            addr = gdb.parse_and_eval(argv[0]).cast(
                    gdb.lookup_type('void').pointer())

            try:
                data_len = int(gdb.parse_and_eval(argv[1]))
            except ValueError:
                raise gdb.GdbError('dump len must be int value.')

            data = gdb.selected_inferior().read_memory(addr, data_len)
            xxd_dump(addr, data)

except Exception as e:
    if 'gdb' not in sys.modules.keys():
        pass
    else:
        raise e


def load_beep_gdb_commands():
    if 'gdb' not in sys.modules.keys():
        return
    # Find everything named BeepGdbCmd*
    # Note these need to go in a specific order where the parent command
    # is loaded before any children.
    me = sys.modules[__name__]
    beep_cmds = dir(me)
    beep_cmds = [x for x in beep_cmds if x.startswith('BeepGdbCmd')]
    beep_cmds.sort()
    beep_cmds = [getattr(me, x) for x in beep_cmds]
    # Pass if the constructor requires arguments.
    for cls in beep_cmds:
        try:
            cls()
        except:
            pass
    # Add other constructors here.


if __name__ == '__main__':
    load_beep_gdb_commands()

