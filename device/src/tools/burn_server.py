"""Runs burn test on a remote device.

NOTE: This requires the contents openssh-sftp-server_6.1p1-1_ar71xx.ipk to be
installed on the remote device. We should just include that in the image,
but haven't done that yet.

Other requirements:
    /etc/config/system needs to have log_ip set to something that will catch
        the logs.

Run this from src/out (or in the same directory as the burn program):
    python ../tools/burn_server.py root device_rsa beep-00511b.local
"""


import binascii
import datetime
import fcntl
import logging
import os
import paramiko
import select
import sys
import socket
import struct
import threading
import time
import traceback

logger = logging.getLogger(sys.argv[0])
logger.setLevel(logging.DEBUG)
conlog = logging.StreamHandler()
conlog.setLevel(logging.DEBUG)
formatter = logging.Formatter("%(asctime)s - %(name)s - %(module)s:%(lineno)s - %(levelname)s - %(message)s")
conlog.setFormatter(formatter)
logger.addHandler(conlog)


LOOP_PORT = 30099
DATA = 'Z' * (16 * 1024)


def get_ip_address(ifname):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    return socket.inet_ntoa(fcntl.ioctl(
        s.fileno(),
        0x8915,  # SIOCGIFADDR
        struct.pack('256s', ifname[:15])
    )[20:24])

def our_ip():
    return get_ip_address('eth0')

def get_open_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("",0))
    port = s.getsockname()[1]
    s.close()
    return port

def _loop_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setblocking(0)

    server_address = ('0.0.0.0', LOOP_PORT)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(server_address)
    sock.listen(100)

    inputs = [sock]
    outputs = []

    def remove(s):
        if s in inputs:
            inputs.remove(s)
        if s in outputs:
            outputs.remove(s)
        s.close()

    while True:
        readable, writable, exceptional = select.select(inputs, outputs, inputs)
        for s in readable:
            if s is sock:
                connection, client_address = sock.accept()
                connection.setblocking(0)
                print >>sys.stderr, 'connection from', client_address
                inputs.append(connection)
                outputs.append(connection)
            else:
                try:
                    data = s.recv(4096)
                    #print >>sys.stderr, 'received %s from %s' % (len(data), s.getpeername())
                    if not data:
                        print 'receiver closed'
                        remove(s)
                except socket.error as msg:
                    print 'Socket error', msg
                    remove(s)
        for s in writable:
            try:
                s.send(DATA)
            except socket.error as msg:
                print 'Socket error', msg
                remove(s)

        for s in exceptional:
            print >>sys.stderr, 'handling exceptional condition for', s.getpeername()
            inputs.remove(s)
            remove(s)

def loop_server():
    t = threading.Thread(target=_loop_server)
    t.setDaemon(True)
    t.start()

# want this to retry connections

class SSHError(Exception):
    pass


class SSH(object):
    def __init__(self, host, login_info=None):
        self.host = host
        self.client = None
        self.login_info = login_info

    def _connect(self):
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        logger.info('Connecting to %s' % self.host)
        if self.login_info and 'key_filename' in self.login_info:
            os.system('chmod 600 %s' % self.login_info['key_filename'])
        self.client.connect(self.host, **self.login_info)
        self.transport = self.client.get_transport()
        self.files = self.client.open_sftp()

    def _put(self, local, remote):
        if not self.client:
            self._connect()
        self.files.put(local, remote, confirm=True)

    def put(self, local, remote):
        logger.debug('Putting file %s at %s' % (local, remote))
        for i in xrange(3):
            try:
                return self._put(local, remote)
            except (paramiko.SSHException, socket.error) as e:
                logger.warning('Couldn\'t put file %s. Retrying' % local)
                self.client = None
                time.sleep(2**i)
        else:
            logger.error('Couldn\'t put file %s. Retrying' % local)
            raise SSHError()

    def _get(self, remote, local):
        if not self.client:
            self._connect()
        self.files.get(remote, local)

    def get(self, remote, local):
        logger.debug('Getting file %s to %s' % (remote, local))
        for i in xrange(3):
            try:
                return self._get(remote, local)
            except (paramiko.SSHException, socket.error) as e:
                logger.warning('Couldn\'t get file %s. Retrying' % remote)
                self.client = None
                time.sleep(2**i)
        else:
            logger.error('Couldn\'t get file %s. Retrying' % remote)
            raise SSHError()

    def _get_status_output(self, command):
        # TODO: timeout
        logger.debug('Running command %s' % command)

        if not self.client:
            self._connect()

        channel = self.transport.open_session()
        channel.exec_command(command)
        channel.set_combine_stderr(True)
        output = []
        while 1:
            # could block
            out = channel.recv(4096)
            if not out:
                break
            output.append(out)
        # could block
        status = channel.recv_exit_status()

        return status, ''.join(output)

    def get_status_output(self, command):
        for i in xrange(3):
            try:
                return self._get_status_output(command)
            except (paramiko.SSHException, socket.error) as e:
                traceback.print_exc()
                logger.warning('Command failed, retrying: %s' % command)
                self.client = None
                time.sleep(2**i)
        else:
            logger.error('Command failed too many times: %s' % command)
            raise SSHError()

    def _run_daemon(self, command, extra=None):
        full_command = command
        if extra:
            full_command = '%s %s' % (command, extra)
        logger.debug('Running daemon %s' % full_command)

        if not self.client:
            self._connect()

        daemon_command = 'nohup %s &' % full_command
        channel = self.transport.open_session()
        channel.exec_command(daemon_command)

        pid_cmd = "pgrep -f '%s'" % command
        s, o = self.get_status_output(pid_cmd)

        pids = None
        if s == 0:
            pids = o.strip().split('\n')
        else:
            logger.warning('Couldn\'t get pids for: %s' % command)
        return pids

    def run_daemon(self, command, extra=None):
        for i in xrange(3):
            try:
                return self._run_daemon(command, extra=extra)
            except (paramiko.SSHException, socket.error) as e:
                logger.warning('Command failed, retrying: %s' % command)
                self.client = None
                time.sleep(2**i)
        else:
            logger.error('Command failed too many times: %s' % command)
            raise SSHError()


class LongCommand(object):
    def __init__(self, command, ssh):
        logger.info('Running LongCommand %s\n' % command)
        self.command = command
        self.ssh = ssh

        command0 = command.split()[0]

        error = False
        try:
            self.pids = self.ssh.run_daemon(
                command,
                extra='2>&1 | logger -t %s' % command0)

            if not self.pids:
                error = True
        except SSHError:
            error = True

        if error:
            logger.error('LongCommand failed to start, retrying %s' % command)
            raise SSHError()

    def stop(self):
        logger.info('Stopping LongCommand %s\n', self.command)
        s, o = self.ssh.get_status_output('kill %s' % ' '.join(self.pids))
        if s != 0:
            logger.warning('Failed to term LongCommand %s\n' % self.command)
            s, o = self.ssh.get_status_output('kill -9 %s' % ' '.join(self.pids))
            if s != 0:
                logger.error('Failed to kill LongCommand %s\n' % self.command)


class TargetHost(object):
    def __init__(self, host, login_info=None):
        self.host = host
        self.ssh = SSH(host, login_info)

    def install(self):
        self.ssh.put('burn', '/tmp/burn')
        self.ssh.get_status_output('opkg update; opkg install memtester')
        s, o = self.ssh.get_status_output('chmod 755 /tmp/burn')

    def clean(self):
        logger.info('Cleaning')
        self.ssh.get_status_output('/etc/init.d/beepmanager stop')
        s, o = self.ssh.get_status_output('rm /burndisk.burn')
        s, o = self.ssh.get_status_output('pgrep burn')
        if s == 0:
            pids = o.strip().split('\n')
            logger.info('Killing pids %s' % pids)
            s, o = self.ssh.get_status_output('kill -9 %s' % ' '.join(pids))
        s, o = self.ssh.get_status_output('killall memtester')
        s, o = self.ssh.get_status_output('rm -rf /tmp/burn.*')

    def stop_burn(self):
        self.burn.stop()

    def start_burn(self, args, logfile):
        self.burn = LongCommand('/tmp/burn %s' % args, self.ssh)

    def start_memtester(self):
        self.memtester = LongCommand('memtester 32', self.ssh)

    def stop_memtester(self):
        self.memtester.stop()


class MultipleHosts(object):
    def __init__(self):
        self.hosts = []

    def add_host(self, host):
        self.hosts.append(host)

    def do_all(self, todo, *args):
        exception_count = 0
        for h in self.hosts:
            try:
                getattr(h, todo)(*args)
            except:
                traceback.print_exc()
                exception_count += 1
        if exception_count == len(self.hosts):
            sys.exit(1)

    def install(self):
        self.do_all('install')

    def clean(self):
        self.do_all('clean')

    def stop_burn(self):
        self.do_all('stop_burn')

    def start_burn(self, *args):
        self.do_all('start_burn', *args)

    def start_memtester(self):
        self.do_all('start_memtester')

    def stop_memtester(self):
        self.do_all('stop_memtester')


def log_name(log_dir, iteration, name):
    now = datetime.datetime.now()
    t = now.strftime('%Y%m%d-%H%M%S')
    return '%s/burn-%s-%s-%s.log' % (log_dir, str(iteration).zfill(6), t, name)


def main(argv):
    loop_server()
    #while 1:
    #    time.sleep(10000)

    username = None
    key_fname = None
    username = argv[1]
    key_fname = argv[2]

    host = MultipleHosts()
    for hostname in argv[3:]:
        host.add_host(TargetHost(hostname, {'key_filename': key_fname,
                                             'username': username}))
    host.clean()
    host.install()

    server_ip = our_ip()
    print server_ip

    now = datetime.datetime.now()
    log_dir = 'logs.%s' % now.strftime('%Y%m%d-%H%M%S')
    try:
        os.mkdir(log_dir)
    except OSError:
        pass

    disk_flag = '--disk=/burndisk.burn'
    cpu_flag = '--cpu'
    ram_flag = '--ram'
    netread_flag = '--netread=%s:%s' % (server_ip, LOOP_PORT)
    netwrite_flag = '--netwrite=%s:%s' % (server_ip, LOOP_PORT)

    test_time = 10 * 60   # 10 minutes
    for i in xrange(999999):
        host.start_burn(disk_flag, log_name(log_dir, i, 'disk'))
        time.sleep(test_time)
        host.stop_burn()
        host.clean()
        time.sleep(2)

        host.start_burn(cpu_flag, log_name(log_dir, i, 'cpu'))
        time.sleep(test_time)
        host.stop_burn()
        host.clean()
        time.sleep(2)

        host.start_burn(ram_flag, log_name(log_dir, i, 'ram'))
        time.sleep(test_time)
        host.stop_burn()
        host.clean()
        time.sleep(2)

        host.start_burn(netread_flag, log_name(log_dir, i, 'netread'))
        time.sleep(test_time)
        host.stop_burn()
        host.clean()
        time.sleep(2)

        host.start_burn(netwrite_flag, log_name(log_dir, i, 'netwrite'))
        time.sleep(test_time)
        host.stop_burn()
        host.clean()
        time.sleep(2)

        host.start_memtester()
        host.start_burn(
                ' '.join((netread_flag, netwrite_flag)),
                log_name(log_dir, i, 'netwrite_netread'))
        time.sleep(test_time)
        host.stop_burn()
        host.stop_memtester()
        host.clean()
        time.sleep(2)

        host.start_burn(
                ' '.join((disk_flag, cpu_flag, ram_flag, netread_flag, netwrite_flag)),
                log_name(log_dir, i, 'all'))
        time.sleep(60 * 60)
        host.stop_burn()
        host.clean()
        time.sleep(2)


if __name__ == '__main__':
    main(sys.argv)
