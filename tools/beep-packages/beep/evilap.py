import os
import re
import spur
import sys
import copy

DEFAULT_HOSTNAME = os.environ.get('BEEP_TEST_AP_HOST', 'test-ap.invalid')
DEFAULT_USERNAME = 'root'
DEFAULT_KEY_FILE = os.environ.get('BEEP_SSH_KEY', '~/.ssh/id_rsa')
DEFAULT_IFROOT = 'wlan0'
DEFAULT_IP_IF = 'br-lan'

MAC_RE = r'([\dA-F]{2}(:[\dA-F]{2}){5})'
IP_RE = r'([0-9]\d{0,2}(\.[0-9]\d{0,2}){3})'
INET_PREFIX = 'inet addr:'
MASK_PREFIX = 'Mask:'

INFO_D = {
    'delay': [None, None, None],
    'loss': [None, None],
    'duplicate': [None, None],
    'corrupt': [None, None],
    'rate': [None]
}

class EvilAp(object):
    def __init__(self, **kwargs):
        """Start a evilap instance.

        Args:
            hostname: hostname of the evilap
            username: ssh username
            password: ssh password
            key_file: path to key file (given precedent over password)
            require_known_host: bool if require server info in known_hosts
            ifroot: root of the evil interfaces
            ip_if: interface with ip (br-lan on OpenWrt routers)
        Returns:
            object
        """
        if kwargs.has_key('password') and not kwargs.has_key('key_file'):
            self.use_pass = True
        else:
            self.use_pass = False
        self.hostname = kwargs.get('hostname', DEFAULT_HOSTNAME)
        self.username = kwargs.get('username', DEFAULT_USERNAME)
        self.password = kwargs.get('password', None)
        self.key_file = os.path.realpath(os.path.expanduser(
                kwargs.get('key_file', DEFAULT_KEY_FILE)))
        self.require_known_host = kwargs.get('require_known_host', False)
        self.ifroot = kwargs.get('ifroot', DEFAULT_IFROOT)
        self.ip_if = kwargs.get('ip_if', DEFAULT_IP_IF)
        spur_args = {}
        spur_args['hostname'] = self.hostname
        spur_args['username'] = self.username
        if (self.use_pass):
            spur_args['password'] = self.password
        else:
            spur_args['private_key_file'] = self.key_file

        if (self.require_known_host == False):
            spur_args['missing_host_key'] = spur.ssh.MissingHostKey.accept

        spur_args['connect_timeout'] = 5

        self.shell = spur.SshShell(**spur_args)

        o = self._cmd(['ifconfig']).split('\n')
        self.interfaces = [x.split(' ')[0] for x in o if
                x.startswith(self.ifroot)]
        if len(self.interfaces) == 0:
            raise Exception('no interfaces found matching {}'.format(
                    self.ifroot))
        self.ip = None
        self.mask = None  # Number of bits
        self.net = None
        for index in xrange(len(o)):
            # Look for space after interface for exact match.
            if o[index].startswith(self.ip_if + ' '):
                ip = re.search(INET_PREFIX + IP_RE, o[index + 1], re.I)
                mask = re.search(MASK_PREFIX + IP_RE, o[index + 1], re.I)
                if None in [ip, mask]:
                    raise Exception('could not find ip/mask for \'{}\''.format(o[index + 1]))
                self.ip = ip.group()[len(INET_PREFIX):]
                mask = mask.group()[len(MASK_PREFIX):].split('.')
                self.mask = sum([bin(int(x)).count('1') for x in mask])
                net = [int(x[0]) & int(x[1]) for x in zip(self.ip.split('.'), mask)]
                self.net = '.'.join([str(x) for x in net])

    def __str__(self):
        keys = ['hostname', 'username', 'password', 'key_file', 'use_pass',
                'ifroot', 'ip_if']
        cfg = ['{}={}'.format(x, self.__getattribute__(x)) for x in keys]
        cfg = ', '.join(cfg)
        return 'EvilAp({})'.format(cfg)

    def _cmd(self, cmd):
        #print('cmd: {}'.format(cmd))
        r = self.shell.run(cmd)
        return r.output

    def _get_arp_table(self):
        # There is no real arp binary on OpenWrt.  This is the same thing.
        o = [x for x in self._cmd(['cat', '/proc/net/arp']).split('\n')[1:] if x != ''
                and x.find('(incomplete)') == -1]
        arp_table = []
        for line in o:
            ip = re.search(IP_RE, line, re.I)
            mac = re.search(MAC_RE, line, re.I)
            if None in [ip, mac]:
                print('Ignoring line \'{}\''.format(line))
                continue
            arp_table.append((ip.group(), mac.group().lower()))
        return arp_table

    def _find_arp_entry(self, mac):
        arp_table = self._get_arp_table()
        matches = [x for x in arp_table if x[1] == mac]
        if len(matches) != 0:
            return matches[0]
        return None

    def _check_interface(self, interface):
        if interface not in self.interfaces:
            raise ValueError('%s not in interfaces' % interface)

    def get_interfaces(self):
        """Get interfaces matching ifroot.

        Args:
            None
        Returns:
            List of matching interfaces
        """
        return list(self.interfaces)

    def get_if_clients(self, interface):
        """Get client mac addresses associated with interface.

        Args:
            interface: full interface name
        Returns:
            list of mac addresses
        """
        self._check_interface(interface)
        o = self._cmd(['iw', 'dev', interface, 'station', 'dump'])
        o = [x for x in o.split('\n') if x.startswith('Station')]
        clients = []
        for line in o:
            mac = re.search(MAC_RE, line, re.I)
            if mac == None:
                print('Ignoring line \'{}\''.format(line))
                continue
            clients.append(mac.group().lower())
        return clients

    def get_client_ip(self, mac):
        """Find client ip from mac address.

        Args:
            mac: mac address (aa:bb:cc:dd:ee:ff)
        Returns:
            ip address

        WARNING: This may take a while if it needs to run nmap.
        """
        mac = mac.lower()
        e = self._find_arp_entry(mac)
        if e != None:
            return e[0]
        # If entry wasn't found run nmap to fill the arp table.
        net_mask = '{}/{}'.format(self.net, self.mask)
        print('refreshing arp table for {}'.format(net_mask))
        self._cmd(['nmap', '-sn', net_mask])
        e = self._find_arp_entry(mac)
        if e != None:
            return e[0]
        return None

    def _parse_tc_line(self, info, line):
        tokens = [x for x in line.split(' ') if x != '']
        key = None
        offset = 0
        for tok in tokens:
            # New key set info list.
            if tok in info.keys():
                key = tok
                offset = 0
            elif key != None:
                if tok[0].isdigit() == True:
                    if offset >= len(info[key]):
                        print('Extra values for \'{}\''.format(key))
                        key = None
                    else:
                        n = tok.strip('msbit%')
                        if n.isdigit():
                            info[key][offset] = int(n)
                        else:
                            info[key][offset] = float(n)
                        offset += 1
                else:
                    # This will be the case of an known keyword while
                    # and is fine.
                    key = None

    def get_evil_info(self, interface):
        """Get evil info for an interface.

        Args:
            interface: full interface name
        Returns:
            dict of evil info (see set_evil_info for value meaning).
        """
        self._check_interface(interface)
        info = copy.deepcopy(INFO_D)
        o = self._cmd(['tc', 'qdisc', 'show', 'dev', interface]).split('\n')
        for line in o:
            if line.startswith('qdisc netem') or line.startswith('qdisc tbf'):
                self._parse_tc_line(info, line)
        return info

    def is_evil(self, interface):
        self._check_interface(interface)
        info = self.get_evil_info(interface)
        for v in info.values():
            if len([x for x in v if x != None]) != 0:
                return True
        return False

    def clear_evil_info(self, interface=None):
        """Reset evil info for an interface.

        Args:
            interface: full interface name, or None for all interfaces.
        Returns:
            None
        """
        if not interface:
            interfaces = self.interfaces
        else:
            self._check_interface(interface)
            interfaces = [interface]
        for interface in interfaces:
            try:
                self._cmd(['tc', 'qdisc', 'del', 'dev', interface, 'root'])
            except spur.RunProcessError as e:
                # ignore return code of 2, which means root did not exist
                # (if that's true the interface was already clear)
                if e.return_code != 2:
                    raise

    def _trim_vals(self, v):
        nl = [(x == None) for x in v]
        if nl != sorted(nl):
            raise ValueError('\'{}\' contains None before value'.format(v))
        rv = [x for x in v if x != None]
        if False in [isinstance(x, int) or
                isinstance(x, float) or
                isinstance(x, long) for x in rv]:
            raise ValueError('\'{}\' is not all numbers'.format(v))
        return rv

    def set_evil_info(self, **kwargs):
        """Set evil info for an interface.

        Args:
            interface: full interface name
            delay: (delay <ms>, variation <ms>, correlation <percent>)
            loss: (loss <percent>, correlation <percent>)
            duplicate: (dups <percent>, correlation <percent>)
            corrupt: (corrupt <percent>, correlation <percent>)
            rate: (max rate <bits/sec>)

            Not all keys are required or all values within the key tuple.
            None cannot come before numerical values:
                (100, None) == OK
                (100,)      == OK
                (None, 100) == BAD
        Returns:
            dict of evil info

        Examples:
            set_evil_info(interface='wlan0', loss=(3, 25), delay=(100, 20))
                Sets the loss at 3% packets with 25% correlation, and delay to
                100ms +- 20ms.
            set_evil_info(interface='wlan0', rate=(1000000,))
                Sets the max rate for the entire interface to 1Mbit/s.  If
                multiple devices are on the interface they both will share the
                bandwidth.

            higher correlation causes the random generator to be more 'bursty'
            prob(c) = corr * (prob - 1) + (1 - corr) * rand
        """
        interface = kwargs['interface']
        self._check_interface(interface)
        sinfo = {}
        netem = []
        tbf = []
        for k in INFO_D.keys():
            if not kwargs.has_key(k):
                continue
            vals = self._trim_vals(kwargs[k])
            if len(vals) == 0:
                continue
            if len(vals) > len(INFO_D[k]):
                raise ValueError('too many values for \'{}\''.format(k))

            if k == 'rate':
                bv = vals[0]
                vals[0] = str(vals[0]) + 'bit'
                # burst determines the size of the token bucket.  The min
                # required should be the bit rate / timer rate (100Hz).
                # If the rate limiter is running too slow up the burst size.
                vals += ['burst', str(bv / 100) + 'b']
                # This also determines the bucket size but 1ms should be
                # good for all cases we need.  Delay will be introduced with
                # netem.
                vals += ['lat', '1ms']
                tbf += [k,] + vals
            elif k in ['loss', 'duplicate', 'corrupt']:
                vals = [str(x) + '%' for x in vals]
                netem += [k,] + vals
            else:
                vals[0] = str(vals[0]) + 'ms'
                if len(vals) >= 2:
                    vals[1] = str(vals[1]) + 'ms'
                if len(vals) >= 3:
                    vals[2] = str(vals[2]) + '%'
                netem += [k,] + vals

        if self.is_evil(interface):
            self.clear_evil_info(interface)

        # Need to have the netem setup since tbf will always point to the
        # parent node.
        if len(netem) == 0:
            netem = ['delay', '0ms']

        netem = ['tc', 'qdisc', 'add', 'dev', interface, 'root', 'handle',
                '1:0', 'netem'] + netem
        self._cmd(netem)

        if len(tbf) != 0:
            tbf = ['tc', 'qdisc', 'add', 'dev', interface, 'parent', '1:1',
                    'handle', '10:', 'tbf'] + tbf
            self._cmd(tbf)

        return self.get_evil_info(interface)
