#!/usr/bin/env python
import requests, json, shlex, sys, code, socket, spur, os, time, imp, paramiko
from beep.evilap import EvilAp
from beep import system
from beep import virtual_device

agents = (
    'beep.app.pandora',
    'beep.app.webradio',
    'beep.distributor',
    'beep.head',
    'beep.health',
    'beep.manager',
    'beep.playnet',
    'beep.comm',
    'beep.comm.control',
    'urelay',
)
real_devices = all_devices = []

__EVIL_AP__ = None
_KEY_FILE = os.path.expanduser(os.environ.get('BEEP_SSH_KEY', '~/.ssh/id_rsa'))

def load_evilap():
    """ Attempts to connect to EvilAP, only needs to be called once. """
    global __EVIL_AP__

    if not has_evilap():
        __EVIL_AP__ = EvilAp()
    return __EVIL_AP__

def has_evilap():
    """ Returns True if EvilAP is available """
    return __EVIL_AP__ != None

def clear_evilap():
    __EVIL_AP__.clear_evil_info()


# low_level_head_call(...)
#
#     Provides access to JSON-RPC interface on each device
def low_level_head_call(host, obj, method, params=None, timeout=20):
    """Uses JSON-RPC to call obj::method on host with params"""
    if not params:
        params = {}

    payload = {
        'jsonrpc' : '2.0',
        'method' : 'call',
        'params' : [
            obj,
            method,
            params
        ]
    }

    url = 'http://%s/synapse' % host

    try:
        r = requests.post(url, json.dumps(payload), timeout=timeout)
        if r.status_code == 200:
            result = r.json()['result']
            if len(result) < 2:
                return None
            else:
                return result[1]
        else:
            return None
    except:
        return None

# head_call(...)
#
#     Wraps low_level_head_call to call beep object methods through the
#     head interface
def head_call(host, context, obj, method, params=None):
    """Uses JSON-RPC to "head-call" obj::method for the given context through interface @ host"""

    if not params:
        params = {}

    head_params = {
        'context' : context,
        'object' : obj,
        'method' : method,
        'params': params
    }

    return low_level_head_call(host, 'beep.head', 'call', head_params)

def _find_device_by_host(host):
    for d in all_devices:
        if d['host'] == host:
            return d
    return None

def get_field(obj, path_str):
    """ Recursively walks down obj (a dict) and retrieves the value at the given path """
    return _get_field(obj, path_str.split(' '))

def _get_field(obj, path):
    if path[0] not in obj:
        return None
    else:
        if len(path) == 1:
            return obj[path[0]]
        else:
            return _get_field(obj[path[0]],path[1:])

def _match_agent(pattern):
    n_matches = 0
    _agent = None

    for agent in agents:
        if agent.find(pattern) >= 0:
            _agent = agent
            n_matches += 1

    if n_matches < 1:
        raise ValueError('No agent matches pattern: %s' % (pattern,))
    elif n_matches > 1:
        raise ValueError('Multiple agents match pattern: %s' % (pattern,))
    else:
        return _agent

def get_state(host, obj):
    agent = _match_agent(obj)
    method = '_get_state' if (agent == 'beep.head') \
            else 'get_state'
    return low_level_head_call(host, agent, method, timeout=5)

def get_state_field(host, obj, path):
    state = get_state(host, obj)
    if not state:
        return None
    return get_field(state['result'], path)


def assert_state(host, obj, path, operand, failmsg=None, test=lambda v, o: v == o):
    val = get_state_field(host, obj, path)
    if not test(val, operand):
        _failmsg = failmsg.replace("$val",str(val)).replace("$op",str(operand)) \
                if failmsg else 'Unspecified'
        raise AssertionError(_failmsg)

    return True

# dev_*(...)
#
#     The dev_* signifies these functions only work on physical devices.  They
#     are implemented outside of the Device object so that they can be used
#     without first defining the device in params.py.
#
#     Alternatively, a user can manually instantiate a RealDevice object:
#
#     x = beep_interface.RealDevice('beep-123456.local','wlan0')
#
#     and use that object like any initialized RealDevice object.

class CommandTimeout(Exception):
    pass

# cache connections, access to this is threadsafe:
#     http://effbot.org/pyfaq/what-kinds-of-global-value-mutation-are-thread-safe.htm
conns = {}
def dev_cmd(host, cmd, user='root',
        use_password=False, no_log=False, timeout=None):
    if timeout == None:
        timeout = sys.maxint
    #print 'DEV_CMD: %s %s' % (host, cmd)
    tries = 0
    shell = None
    if host in conns:
        shell = conns[host]
    while True:
        try:
            if not shell:
                shell_args = {
                        'hostname': host,
                        'username': user,
                        'connect_timeout': 15,
                        'shell_type': spur.ssh.ShellTypes.minimal,
                        'missing_host_key': spur.ssh.MissingHostKey.accept}
                if use_password:
                    password = use_password
                    if use_password == True:
                        password = os.environ['BEEP_SSH_PASSWORD']
                    shell_args['password'] = password
                else:
                    shell_args['private_key_file'] = _KEY_FILE
                #print 'DEV_CMD: creating shell %s %s' % (host, cmd)
                shell = spur.SshShell(**shell_args)
                #print 'DEV_CMD: done creating shell %s %s' % (host, cmd)
            #print 'DEV_CMD: RUN command: %s %s' % (host, cmd)
            process = shell.spawn(shlex.split(cmd))
            #print 'DEV_CMD: DONE SPAWN command: %s %s' % (host, cmd)
            start_time = time.time()
            while time.time() - start_time < timeout and process.is_running():
                time.sleep(0.1)
            if not process.is_running():
                #print 'DEV_CMD: CALLING WAIT FOR RESULT command: %s %s' % (host, cmd)
                result = process.wait_for_result()
                #print 'DEV_CMD: DONE CALLING WAIT FOR RESULT command: %s %s' % (host, cmd)
            else:
                #print 'DEV_CMD: command timeout!: %s %s' % (host, cmd)
                #process.send_signal(9)
                raise CommandTimeout
            #print 'DEV_CMD: DONE RUNNING command: %s %s' % (host, cmd)

            # success!
            conns[host] = shell

            return result
        except (spur.ssh.ConnectionError, paramiko.ssh_exception.SSHException,
                socket.error) as e:
            if e == socket.error and e.errno != errno.ECONNRESET:
                raise
            #print 'DEV_CMD failed: %s %s %s' % (host, cmd, tries)
            if tries == 3:
                raise
            shell = None
            tries += 1
            time.sleep(tries * 2)

def dev_ubus_call(host, obj, method, params):
    return dev_cmd(host, """ubus call %s %s '%s'""" % \
            (obj, method, json.dumps(params)))

def dev_ubus_list(host):
    return dev_cmd(host, """ubus list""") 

def dev_start_babysitter(host):
    return dev_cmd(host, '/etc/init.d/beepmanager start')

def dev_sigkill_process(host, mask):
    return dev_cmd(host,
            "ps -o pid,comm,args | " + \
            "grep -v grep | " + \
            "grep " + mask + " | " + \
            "awk '{print $1}' | " + \
            "xargs kill -SIGKILL")

def dev_sigterm_process(host, mask):
    return dev_cmd(host,
            "ps -o pid,comm,args | " + \
            "grep -v grep | " + \
            "grep " + mask + " | " + \
            "awk '{print $1}' | " + \
            "xargs kill -SIGTERM")

def dev_sigkillall_process(host, mask):
    return dev_cmd(host, 'killall -q -SIGKILL %s' % mask)

# This function requires playnet to be alive
def dev_sigkill_all(host):
    return dev_cmd(host,
            "/etc/init.d/beepmanager stop || /bin/true && " + \
            "sleep 1 && " + \
            "ps -o comm,pid,pgid | " + \
            "grep -v grep | " + \
            "grep playnet | " + \
            "awk '{print -$3}' | " + \
            "xargs kill -SIGKILL")

# This function requires playnet to be alive
def dev_sigterm_all(host):
    return dev_cmd(host,
            "/etc/init.d/beepmanager stop && " + \
            "sleep 1 && " + \
            "ps -o comm,pid,pgid | " + \
            "grep -v grep | " + \
            "grep playnet | " + \
            "awk '{print -$3} | ' " + \
            "xargs kill -SIGTERM")

def dev_start(host):
    return dev_start_babysitter(host)

def dev_stop(host):
    dev_cmd(host, "/etc/init.d/beepmanager stop")

    # restart mdnsd to clear it's cache. We were sometimes getting stale text
    # records from the previous run, which would cause cluster glitches
    # when stopping and starting devices.
    dev_cmd(host, '/etc/init.d/mdnsd restart')

def _assert_evilap():
    if not has_evilap():
        raise ValueError('EvilAP is unavailable.')

def dev_set_evil_info(host, evil_info, evil_interface=None):
    _assert_evilap()

    iface = None

    if evil_interface:
        iface = evil_interface
    else:
        d = _find_device_by_host(host)
        iface = d['interface']

    evil_info['interface'] = iface
    __EVIL_AP__.set_evil_info(**evil_info)

def dev_set_delay(host,
                 delay_ms=200,
                 variation_percent=None,
                 correlation_percent=None,
                 evil_interface=None):
    dev_set_evil_info(host,
            {'delay': (delay_ms, variation_percent, correlation_percent)},
            evil_interface=evil_interface)

def dev_set_loss(host,
                 loss_percent=100,
                 correlation_percent=None,
                 evil_interface=None):
    dev_set_evil_info(host,
            {'loss': (loss_percent, correlation_percent)},
            evil_interface=evil_interface)

def dev_restore_network(host,evil_interface=None):
    _assert_evilap()

    iface = None

    if evil_interface:
        iface = evil_interface
    else:
        d = _find_device_by_host(host)
        iface = d['interface']

    __EVIL_AP__.clear_evil_info(iface)

def dev_uci_set(host, section, optname, value):
    cmd = 'uci set beep_%s.main.%s=%s' % (section, optname, value)
    try:
        dev_cmd(host, cmd)
        return True
    except spur.results.RunProcessError:
        return False

def dev_uci_commit(host):
    try:
        dev_cmd(host, 'uci commit beep_apps')
        dev_cmd(host, 'uci commit beep_data')
        dev_cmd(host, 'uci commit beep_devel')
        dev_cmd(host, 'uci commit beep_device')
        dev_cmd(host, 'uci commit beep_static')
        return True
    except spur.results.RunProcessError:
        return False

def dev_uci_get(host, section, optname):
    cmd = 'uci get beep_%s.main.%s' % (section, optname)
    try:
        return dev_cmd(host, cmd).output.strip()
    except spur.results.RunProcessError:
        return None

class Device(object):
    def __init__(self, host):
        self.host = host
        self.id = None
        self.evil_interface = None

    def get_state(self, obj='beep.manager'):
        """Returns the state of obj as a dict"""
        r = get_state(self.host, obj)
        return r['result'] if r else None

    def get_state_field(self, obj, path):
        return get_state_field(self.host, obj, path)

    def assert_state(self, *args, **kwargs):
        """Calls object::get_state and returns (r,v) where r is the result of
        test(value, operand)

        Parameters:
            obj         ubus object name, e.g., beep.manager, beep.app.some_app
            path        path to field of interest in object's state object
            operand     the "other" value to test against
            test        a function that takes two parameters, value of the field
                        at path and operand

        Returns:
            (result,    boolean result of test(value, operand)
            value)      value of the field at path

        """
        return assert_state(self.host, *args, **kwargs)

    def wait_for_state(self, obj, path, operand, failmsg=None, \
            test=lambda v,o: v == o, interval=0.5, timeout=20):
        wait_start = time.time()

        while True:
            try:
                self.assert_state(obj, path, operand, failmsg, test)
                break
            except AssertionError:
                pass
            if timeout > 0 and time.time() - wait_start > timeout:
                _failmsg = failmsg.replace("$op",str(operand)) \
                        .replace("$timeout",str(timeout)) \
                        if failmsg else 'Waiting for state timed out'
                raise AssertionError(_failmsg )
            else:
                time.sleep(interval)
        return True

    def head_call(self, context, obj, method, params):
        return head_call(self.host, context, obj, method, params)

    def dtab(self):
        if not self.id:
            self.id = self.uci_get('device', 'device_id')
        return {self.id:self}

    def uci_set(self, section, optname, value):
        raise NotImplementedError()

    def uci_commit(self):
        raise NotImplementedError()

    def uci_get(self, section, optname):
        raise NotImplementedError()

    def is_real_device(self):
        raise NotImplementedError()

    def start(self):
        raise NotImplementedError()

    def stop(self):
        raise NotImplementedError()

    def ssh(self, cmd):
        raise NotImplementedError()

    def ubus_call(self, obj, method, params):
        raise NotImplementedError()

    def ubus_list(self):
        raise NotImplementedError()

    def term_process(self, process):
        raise NotImplementedError()

    def kill_process(self, process):
        raise NotImplementedError()

    def kill_network(self):
        raise NotImplementedError()

    def restore_network(self):
        raise NotImplementedError()

class RealDevice(Device):
    def __init__(self, host, evil_interface=None):
        super(RealDevice, self).__init__(host)
        self.evil_interface = evil_interface

    def is_real_device(self):
        return True

    def start(self):
        """Starts beep services (N.B. This method is asynchronous)"""
        return dev_start(self.host)

    def stop(self):
        """Stops all beep services"""
        return dev_stop(self.host)

    def ssh(self, cmd):
        """Issues [cmd] to device"""
        return dev_cmd(self.host, cmd)

    def ubus_call(self, obj, method, params):
        """Perform method call directly on device ubus and return text output"""
        return dev_ubus_call(self.host, obj, method, params).output

    def ubus_list(self):
        """List components on virtual device's ubus"""
        return dev_ubus_list(self.host).output.split()

    def term_process(self, process):
        """Sends a SIGTERM to the first result of grep'ing ps for [process]"""
        return dev_sigterm_process(self.host, process)

    def kill_process(self, process):
        """Sends a SIGKILL to the first result of grep'ing ps for [process]"""
        return dev_sigkill_process(self.host, process)

    def set_delay(self,
                  delay_ms=200,
                  variation_percent=None,
                  correlation_percent=None):
        dev_set_delay(
                self.host,
                delay_ms=delay_ms,
                variation_percent=variation_percent,
                correlation_percent=correlation_percent,
                evil_interface=self.evil_interface)

    def set_loss(self, loss_percent=100, correlation_percent=None):
        """Sets this device's interface to drop X% of packets"""
        dev_set_loss(
                self.host,
                loss_percent=loss_percent,
                correlation_percent=correlation_percent,
                evil_interface=self.evil_interface)

    def restore_network(self):
        """Clears all EvilAp settings for this device's interface"""
        dev_restore_network(self.host, self.evil_interface)

    def uci_set(self, section, optname, value):
        """Set uci option beep_<section>.main.<optname>=<value>"""
        return dev_uci_set(self.host, section, optname, value)

    def uci_commit(self):
        """Commit any changes to flash -- not necessary for changes to take effect"""
        return dev_uci_commit(self.host)

    def uci_get(self, section, optname):
        """Get value of uci option beep_<section>.main.<optname>"""
        return dev_uci_get(self.host, section, optname)

class VirtualDevice(Device):
    def __init__(self, host, path):
        super(VirtualDevice, self).__init__(host)
        self.path = os.path.abspath(path)

        if not os.environ.get('LUA_CPATH'):
            sys.stderr.write('Environment variable LUA_CPATH not present, did you source env.sh? Exiting...\n')
            sys.exit(1)

    def is_real_device(self):
        return False

    def start(self):
        """Starts beep services (N.B. This method is asynchronous)"""
        virtual_device.start(self.path)

    def stop(self):
        """Stops all beep services"""
        virtual_device.stop(self.path)

    def ubus_call(self, obj, method, params):
        """Perform method call directly on virtual device's ubus"""
        return virtual_device.ubus_call(self.path, obj, method, params)
        #return virt_boom_cmd(self.path, 'ubus -- call %s %s \'%s\'' % \
        #        (obj, method, json.dumps(params)))

    def ubus_list(self):
        """List components on virtual device's ubus"""
        return virtual_device.ubus_list(self.path)
        #return virt_boom_cmd(self.path, 'ubus -q -- list').split()

    def uci_set(self, section, optname, value):
        return system.uci_set(self.path, section, optname, value)

    def uci_commit(self):
        return system.uci_commit(self.path)

    def uci_get(self, section, optname):
        return system.uci_get(self.path, section, optname)

    #def term_process(self, process):
    #    """Sends a SIGTERM to the first result of grep'ing ps for [process]"""
    #    return dev_sigterm_process(self.path, process)

    #def kill_process(self, process):
    #    """Sends a SIGKILL to the first result of grep'ing ps for [process]"""
    #    return virt_sigkill_process(self.path, process)

class DeviceTable(dict):
    """Subclass of dict provides helper functions to grab only real or virtual devices"""
    def real_devices(self):
        d = {}
        for dev_id,dev in self.iteritems():
            if dev.is_real_device():
                d[dev_id] = dev
        return d

    def virt_devices(self):
        d = {}
        for dev_id,dev in self.iteritems():
            if not dev.is_real_device():
                d[dev_id] = dev
        return d

if __name__ == '__main__':
    welcome_msg = """
Beep Control Interface
----------------------

dtab is a DeviceTable of discovered devices.  Use dev(...) to quickly access
them.

Press Ctrl+D to exit"""

    def dev(suffix, case_sensitive=False):
        cond = (lambda x,y: x.endswith(y)) if case_sensitive else \
                (lambda x,y: x.lower().endswith(y.lower()))
        for dev_id in dtab.keys():
            if cond(dev_id, suffix):
                return dtab[dev_id]
        raise NotFoundError('No devices ending with %s' % suffix)

    print welcome_msg

    sys.stderr.write('Discovering devices...')
    from beep.discovery import get_real_device_table
    dtab = get_real_device_table()
    sys.stderr.write('done. %d devices found' % (len(dtab),))
    try:
        __IPYTHON__
    except NameError:
        code.interact(local=locals(),banner='')
    else:
        from IPython.terminal.embed import InteractiveShellEmbed
        InteractiveShellEmbed(banner1='')()
