#!/usr/bin/env python
import json
import os
import sys
import threading

from beep import system
from beep import utils


class VirtualDeviceError(Exception):
    pass



##### beep virtual device specific functions

def get_agent_pids(name, devname, allow_multiple, ps_command):
    try:
        result = system.run(ps_command)
        result = result.strip().split('\n')
        if allow_multiple:
            return [int(l.split()[0].strip()) for l in result]

        assert(len(result) != 0)  # would have gotten a CommandError
        if len(result) > 1:
            print('Error: Found more than one pid for %s %s. '
                  'Dumping output:'
                    % (devname, name))
            print('\n'.join(result))
            raise VirtualDeviceError()

        return int(result[0].split()[0].strip())
    except system.CommandError:
        if allow_multiple:
            return []
        else:
            return None

class BeepAgent(object):
    """Represents a beep component, like playnet or ubusd"""
    def __init__(self, name):
        self.name = name

    def get_pid(self, devname, allow_multiple=False):
        # ps ax | grep [p]rocessname, this ensures that grep itself doesn't
        # show up in the output
        ps_command = 'ps ax --no-headers | grep "[%s]%s.*\/%s\.ubus"' % (
                self.name[0], self.name[1:], devname)
        return get_agent_pids(self.name, devname, allow_multiple, ps_command)

    def start(self, path):
        # most agents get started by beepmanager
        pass

class PlaynetChildAgent(BeepAgent):
    def __init__(self, *args, **kwargs):
        super(PlaynetChildAgent, self).__init__(*args, **kwargs)

    def get_pid(self, devname, allow_multiple=False):
        # ps ax | grep [p]rocessname, this ensures that grep itself doesn't
        # show up in the output
        playnet_pid = BeepAgent('playnet').get_pid(devname)
        if not playnet_pid:
            if allow_multiple:
                return []
            else:
                return None
        ps_command = 'ps --no-headers --ppid %s' % playnet_pid
        return get_agent_pids(self.name, devname, allow_multiple, ps_command)

def run_agent(agent_name, agent_command, log_path):
    cmd = '%s 2>&1 | cat > %s/%s.log' % (
        agent_command, log_path, agent_name)
    system.run_no_output(cmd)

class UbusdAgent(BeepAgent):
    def __init__(self, *args, **kwargs):
        super(UbusdAgent, self).__init__(*args, **kwargs)

    def start(self, path):
        devname = devname_from_path(path)
        cmd = 'ubusd -s %s' % ubus_sock_path(devname)
        run_agent(self.name, cmd, path)

class BeepioAgent(BeepAgent):
    def __init__(self, *args, **kwargs):
        super(BeepioAgent, self).__init__(*args, **kwargs)

    def start(self, path):
        devname = devname_from_path(path)
        cmd = 'lua %s/beepio.lua --ubus=%s --uciconfig=%s' % (
                system.BEEP_LUA_DIR, ubus_sock_path(devname),
                path)
        run_agent(self.name, cmd, path)

class BeepManagerAgent(BeepAgent):
    lock = threading.Lock()

    def __init__(self, *args, **kwargs):
        super(BeepManagerAgent, self).__init__(*args, **kwargs)

    def start(self, path):
        devname = devname_from_path(path)
        playnet_port = system.uci_get_raw(path, 'beep_devel.boom.playnet_port')
        urelay_port = system.uci_get_raw(path, 'beep_devel.boom.urelay_port')
        uhttpd_port = system.uci_get_raw(path, 'beep_devel.boom.uhttpd_port')
        spotify_port = system.uci_get_raw(path, 'beep_devel.boom.spotify_port')
        ssdp_port = system.uci_get_raw(path, 'beep_devel.boom.ssdp_port')
        dial_port = system.uci_get_raw(path, 'beep_devel.boom.dial_port')
        msg_port = system.uci_get_raw(path, 'beep_devel.boom.msg_port')

        cmd = ('lua %s/beepmanager.lua '
               '--vm '
               '--ubus=%s '
               '--uciconfig=%s ' % (
                    system.BEEP_LUA_DIR,
                    ubus_sock_path(devname),
                    path))
        if playnet_port:
            cmd += ' --playnet_port=%s' % playnet_port
        if urelay_port:
            cmd += ' --urelay_port=%s' % urelay_port
        if uhttpd_port:
            cmd += ' --uhttpd_port=%s' % uhttpd_port
        if spotify_port:
            cmd += ' --spotify_port=%s' % spotify_port
        if ssdp_port:
            cmd += ' --ssdp_port=%s' % ssdp_port
        if dial_port:
            cmd += ' --dial_port=%s' % dial_port
        if msg_port:
            cmd += ' --msg_port=%s' % msg_port

        # TODO: Is the bug here, because current dir is a global value?
        with system.ChDir(system.BEEP_BIN_DIR):
            run_agent(self.name, cmd, path)


# make sure ubusd is first so we start it first

BEEP_AGENTS = []
BEEP_AGENTS.append(UbusdAgent('ubusd'))

BEEP_AGENTS += [BeepAgent(a) for a in [
    'urelay',
    'uhttpd',
    'distributor',
    'playnet',
    'beephead',
    'beepcomm',
    'beepcloud',
    'beepdiscovery',
    'beephealth',
    'beepjs',
    'app_spotify',
    'app_webradio']]

#BEEP_AGENTS.append(BeepioAgent('beepio'))
BEEP_AGENTS.append(BeepManagerAgent('beepmanager'))
BEEP_AGENTS.append(PlaynetChildAgent('beepdummy'))
BEEP_AGENTS.append(PlaynetChildAgent('beepalsa'))


def devname_from_path(path):
    return os.path.basename(path)

def ubus_sock_path(devname):
    return '/tmp/%s.ubus' % devname

def get_component_pids(devname):
    pids = []
    for a in BEEP_AGENTS:
        pids += a.get_pid(devname, allow_multiple=True)
    return pids

def stop(path):
    devname = devname_from_path(path)
    pids = get_component_pids(devname)
    system.stop_pids(pids)
    try:
        os.remove('/tmp/%s.ubus' % devname)
    except OSError:
        pass

def is_started(path):
    devname = devname_from_path(path)
    if os.path.exists(ubus_sock_path(devname)):
        return True
    return get_component_pids(devname)

def start(path):
    if is_started(path):
        raise VirtualDeviceError('device is already started: %s' % path)
    for agent in BEEP_AGENTS:
        agent.start(path)

def ubus_call(path, obj, method, params):
    ubus_path = ubus_sock_path(devname_from_path(path))
    return system.run('ubus -s %s call %s %s \'%s\'' % (
        ubus_path, obj, method, json.dumps(params)))

def ubus_list(path):
    ubus_path = ubus_sock_path(devname_from_path(path))
    return system.run('ubus -s %s list' % ubus_path).split()


def uci_cmd(path, cmd):
    return system.run('uci -c %s %s', path, cmd)


def main(argv):
    stop('a/A')

    dev_path = os.path.abspath('A')
    #BeepAgent('ubusd').start(dev_path)
    #BeepManagerAgent('beepmanager').start(dev_path)

    start(dev_path)

    #UbusdAgent('ubusd').start(dev_path)

    #Beepio('beepio').start('../../../device/testing/devs/A')
    #print UbusdAgent('ubusd').get_pid('A')
    #print get_component_pids('a/E')


if __name__ == '__main__':
    main(sys.argv)
