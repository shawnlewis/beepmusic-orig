#!/usr/bin/python
"""
Discover beep devices on the local network via mDNS.  Requires pybonjour
and requests
"""
import select, sys, getopt, datetime, requests, json, threading, sets
from multiprocessing import Queue

import pybonjour
from beep.interface import Device, RealDevice, DeviceTable

def get_real_device_table(timeout=5):
    """Synchronously discover real beeps and return a DeviceTable.

    Optionally, pass a timeout in seconds (default 5s).
    """

    devices = {}
    virtual_set = sets.Set()
    dtab = DeviceTable()

    def info_cb(host, state):
        devices[host] = state

    def resolve_cb(interfaceIndex, fullName, hostTarget, port, txtRecord):
        if 'virtual' in txtRecord and txtRecord['virtual']:
            virtual_set.add(txtRecord['device_id'])

    discovery = Discovery(info=info_cb, resolve=resolve_cb)
    discovery.start(timeout)
    for host,state in devices.iteritems():
        raw_host = host.split(':')[0]
        device_id = state['local_device']['device_id']
        if device_id not in virtual_set:
            dtab[device_id] = RealDevice(raw_host)

    return dtab

def get_devices(timeout=5):
    """Synchronously discover beeps and return a dict of HTTP interface URLs
    and state."""

    devices = {}

    def info_cb(host, state):
        devices[host] = state

    discovery = Discovery(info=info_cb)
    discovery.start(timeout)

    return devices

def _parse_txt_record(txtRecord):
    pos = 0
    records = {}
    while True:
        rec_len = ord(txtRecord[pos])
        parsed = txtRecord[pos+1:pos+1+rec_len].split('=')
        if len(parsed) == 2:
            records[parsed[0]] = parsed[1]
        pos = pos+rec_len+1
        if pos >= len(txtRecord):
            break
    return records

class Discovery(object):
    """Synchronous beep device discovery

    A wrapper for pybonjour that synchronously discovers and gathers information
    about Beep devices on the local network.

    You should create a Discovery object, passing any or all of four callbacks:

    Discovery(add, delete, resolve, info)

        add(interfaceIndex, serviceName, replyDomain, regType)
        delete(interfaceIndex, serviceName, replyDomain, regType)
        resolve(interfaceIndex, fullName, hostTarget, port, txtRecord)
        info(hostTarget, state)

    Beep devices are first added when they are registered via mDNS, then they
    must be resolved, which provides more detailed information about the service
    and its location.  Finally, beep.interface is invoked to poll the newly
    discovered device for information, which is returned in the info(...)
    callback.
    """

    def __init__(self, add=None, delete=None, resolve=None, info=None, quiet=True):
        # Internal dns-sd references
        self._browse_sdRef = -1
        self._resolve_sdRefs = []

        # Device state polling thread management
        self._infoqueue = Queue()

        # Safe shutdown
        self._shutdown_event = threading.Event()
        self._shutdown_event.set()

        # Callbacks (all called from calling thread when synchronous)
        self.add_cb = add
        self.delete_cb = delete
        self.resolve_cb = resolve
        self.info_cb = info

        self.quiet = quiet

    def start(self,timeout=None):
        """Synchronously discover Beep devices

        Optionally, pass a timeout in seconds to stop discovering (default None)
        """

        self._browse_sdRef = pybonjour.DNSServiceBrowse(
                regtype = '_beephttp._tcp',
                callBack = self.__browse_callback)

        end_time = None

        self._shutdown_event.clear()
        if timeout:
            start_time = datetime.datetime.now()
            end_time = start_time + datetime.timedelta(seconds=timeout)
        try:
            while not self._shutdown_event.is_set():
                if timeout and end_time <= datetime.datetime.now():
                    self._shutdown_event.set()
                    break
                ready = select.select(
                        [self._browse_sdRef] + \
                        self._resolve_sdRefs + \
                        [self._infoqueue._reader.fileno()],
                        [], [], 0.1)
                for ref in \
                        [self._browse_sdRef] + \
                        self._resolve_sdRefs:
                    if ref in ready[0]:
                        pybonjour.DNSServiceProcessResult(ref)
                if (self._infoqueue._reader.fileno() in ready[0]) and self.info_cb:
                    while not self._infoqueue.empty():
                        info = self._infoqueue.get()
                        self.info_cb(*info)
        except KeyboardInterrupt:
            pass

        for ref in [self._browse_sdRef] + self._resolve_sdRefs:
            ref.close()

    def start_async(self, timeout=None):
        """asynchronously discover Beeps

        *** callbacks will be called from another thread ***"""
        self.async_thread = threading.Thread(target=self.start,args=(timeout,))
        self.async_thread.start()

    def is_started(self):
        return not self._shutdown_event.is_set()

    def stop(self):
        """stop discovery

        This method is threadsafe."""
        self._shutdown_event.set()

    def __browse_callback(self, sdRef, flags, interfaceIndex, errorCode,
            serviceName, regtype, replyDomain):

        if errorCode != pybonjour.kDNSServiceErr_NoError:
            if not self.quiet:
                sys.stderr.write('Error on browse: %d\n' % errorCode)
            return

        if (flags & pybonjour.kDNSServiceFlagsAdd): # Add service
            if self.add_cb:
                self.add_cb(interfaceIndex, serviceName, replyDomain, regtype)
            self._resolve_sdRefs.append(pybonjour.DNSServiceResolve(
                    interfaceIndex=interfaceIndex,
                    name=serviceName,
                    regtype=regtype,
                    domain=replyDomain,
                    callBack=self.__resolve_callback))
        elif self.delete_cb:
            self.delete_cb(interfaceIndex, serviceName, replyDomain, regtype)

    def __resolve_callback(self, sdRef, flags, interfaceIndex, errorCode,
            fullname, hosttarget, port, txtRecord):
        if errorCode != pybonjour.kDNSServiceErr_NoError:
            if not self.quiet:
                sys.stderr.write('Error on resolve: %d\n' % errorCode)
            return

        if self.resolve_cb:
            txt_record_dict = _parse_txt_record(txtRecord)
            self.resolve_cb(interfaceIndex, fullname, hosttarget, port, txt_record_dict)

        self._resolve_sdRefs.remove(sdRef)

        new_info_thread = threading.Thread(
                target=self.__get_info,
                args=('%s:%d' % (hosttarget, port),))
        new_info_thread.start()

    def __get_info(self, hosttarget):
        device = Device('%s' % (hosttarget,))
        state = device.get_state()
        if state:
            self._infoqueue.put((hosttarget, state))
        else:
            if not self.quiet:
                sys.stderr.write('Failed to retrieve %s state\n' % hosttarget)

def _show_help():
    print 'discovery.py'
    print ''
    print 'Discover beeps'
    print ''
    print 'Usage: python discovery.py [-h|--help] [-t <timeout>|--timeout=<timeout]'
    print ''
    print '    -h,--help        Shows this information and exits'
    print '    -t,--timeout     Sets a time limit for discovery in seconds.  When'
    print '                     this timeout is reached, discovery.py exits'
    print '    -f,--filter      One or more from characters [a]dd,[d]elete,[r]esolve,'
    print '                     [i]nfo that selects which entries to output'

if __name__ == '__main__':
    optspec = 'ht:f:'
    optlongspec = [
            'help',
            'timeout='
            'filter='
    ]
    out_filter = 'adri'
    out_filter_bank = 'adri'
    timeout = None
    end_time = None

    try:
        optlist, args = getopt.getopt(sys.argv[1:], optspec, optlongspec)
    except getopt.GetoptError as e:
        sys.stderr.write('Syntax error: %s\n\n' % str(e))
        _show_help()
        sys.exit(-1)

    for o, arg in optlist:
        if o in ('-h', '--help'):
            _show_help()
            sys.exit(0)
        elif o in ('-t', '--timeout'):
            timeout = int(arg)
        elif o in ('-f', '--filter'):
            out_filter = ''
            for c in arg:
                if c not in out_filter_bank:
                    sys.stderr.write('Unknown filter spec: %c\n' % c)
                    sys.exit(-1)
                else:
                    out_filter = out_filter + c
        else:
            _show_help()
            sys.exit(0)

    def _add_cb(interfaceIndex, serviceName, replyDomain, regType):
        print '+ADD',interfaceIndex, \
                serviceName + '.' + replyDomain,regType

    def _delete_cb(interfaceIndex, serviceName, replyDomain, regType):
        print '-DEL',interfaceIndex, \
                serviceName + '.' + replyDomain,regType

    def _resolve_cb(interfaceIndex, fullName, hostTarget, port, txtRecord):
        print '*RES',interfaceIndex,fullName, \
                hostTarget,port,json.dumps(txtRecord)

    def _info_cb(hostTarget, state):
        print '!INF',hostTarget,state['local_device']

    discovery = Discovery(
            _add_cb if 'a' in out_filter else None,
            _delete_cb if 'd' in out_filter else None,
            _resolve_cb if 'r' in out_filter else None,
            _info_cb if 'i' in out_filter else None)

    discovery.start(timeout=timeout) # This call blocks
