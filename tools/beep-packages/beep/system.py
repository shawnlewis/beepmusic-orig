import os
import subprocess
import time
import sys

BEEP_ROOT_DIR = os.path.abspath(os.path.dirname(__file__) + '/../../..') + '/'
BEEP_BIN_DIR = BEEP_ROOT_DIR + 'device/src/out/host/'
BEEP_LUA_DIR = BEEP_BIN_DIR + 'lua/'

class CommandError(Exception):
    pass

class ChDir:
    def __init__(self, path):
        self.path = path

    def __enter__(self):
        self.cur_dir = os.getcwd()
        os.chdir(self.path)

    def __exit__(self, type, value, traceback):
        os.chdir(self.cur_dir)

def check_pid(pid):
    """ Check For the existence of a unix pid. """
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True

# tries really hard to kill a set of pids
def stop_pids(pids):
    if not pids:
        return
    if isinstance(pids, int):
        pids = [pids]

    pids = set(pids)
    for p in pids:
        try:
            os.kill(p, 15)
        except OSError:
            pass

    # poll pids until they're dead, or 2 seconds have passed
    start = time.time()
    while pids and time.time() < start + 2:
        to_remove = set()
        for p in pids:
            if not check_pid(p):
                to_remove.add(p)
        pids = pids.difference(to_remove)

    for p in pids:
        print('Warning: pid did not die via SIGTERM, '
              'dumping info and killing. %s' % p)
        try:
            print(run('ps -fp %s' % p))
        except CommandError:
            pass
        try:
            os.kill(p, 9)
        except OSError:
            pass

    if pids:
        start = time.time()
        while pids and time.time() < start + 2:
            to_remove = set()
            for p in pids:
                if not check_pid(p):
                    to_remove.add(p)
            pids = pids.difference(to_remove)

    if pids:
        print('Error: could not SIGKILL pid: %s', p)
        return False
    else:
        return True

def run_no_output(cmd):
    #print 'CMD ', cmd
    try:
        out = subprocess.Popen(cmd, shell=True)
        return out
    except subprocess.CalledProcessError as e:
        raise CommandError('command returned %d' % e.returncode)

def run(cmd, path='.'):
    #print 'CMD ', cmd
    with ChDir(path):
        try:
            out = subprocess.check_output(cmd,shell=True)
            return out
        except subprocess.CalledProcessError as e:
            raise CommandError('command returned %d' % e.returncode)

def spawn_daemon(func):
    # From: http://stackoverflow.com/questions/6011235/run-a-program-from-python-and-have-it-continue-to-run-after-the-script-is-kille
    # do the UNIX double-fork magic, see Stevens' "Advanced
    # Programming in the UNIX Environment" for details (ISBN 0201563177)
    try:
        pid = os.fork()
        if pid > 0:
            # parent process, return and keep running
            return
    except OSError, e:
        print >>sys.stderr, "fork #1 failed: %d (%s)" % (e.errno, e.strerror)
        sys.exit(1)

    os.setsid()

    # do second fork
    try:
        pid = os.fork()
        if pid > 0:
            # exit from second parent
            sys.exit(0)
    except OSError, e:
        print >>sys.stderr, "fork #2 failed: %d (%s)" % (e.errno, e.strerror)
        sys.exit(1)

    # do stuff
    func()

    # all done
    os._exit(os.EX_OK)

def uci_cmd(path, cmd):
    return run('uci -c %s %s' % (path, cmd))

def uci_set(path, section, optname, value):
    uci_cmd(path, 'set beep_%s.main.%s=\'%s\'' % \
            (section, optname, value))
    return True

def uci_set_main(path, optname, value):
    return uci_set(path, 'data', optname, value)

def uci_commit(path):
    uci_cmd(path, 'commit beep_data')
    return True

def uci_get_raw(path, optpath):
    return uci_cmd(path, 'get %s' % optpath).strip()

def uci_get(path, section, optname):
    return uci_get_raw(path, 'beep_%s.main.%s' % (section, optname))

def uci_get_main(path, optname):
    return uci_get(path, 'data', optname)
    #try:
    #except:
    #    return None
