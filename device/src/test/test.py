#!/usr/bin/python
import subprocess, shlex, os, re, time, unittest, json, requests, thread
import shutil

STD_TIMEOUT = 10 # seconds
STD_WAIT = 10 # seconds

HOME_DIR = os.getcwd()

PANDORA_A = {'id':'19566453720794262'}
PANDORA_B = {'id':'77277697348912278'}

WEBRADIO_A = {
    'url':'http://ice.somafm.com/groovesalad',
    'name':'SomaFM GrooveSalad' }
WEBRADIO_B = {
    'url':'http://ice.somafm.com/dubstep',
    'name':'SomaFM Dubstep Beyond' }

class TwoMasterWithSlavesTest(unittest.TestCase):
    def test_audio_apps_http(self):
        self.expect_loglines([
            (self.deviceA, 'beephead', '.*Update state:.*app\.pandora'),
            (self.deviceA, 'beephead', '.*Update state:.*app\.webradio'),
            (self.deviceB, 'beephead', '.*Update state:.*app\.pandora'),
            (self.deviceB, 'beephead', '.*Update state:.*app\.webradio')
        ], 10)

        self.head_call_A('app.pandora', 'play_station', PANDORA_A)
        self.head_call_B('app.webradio', 'play_station', WEBRADIO_A)

        self.expect_loglines([
            (self.deviceA, 'playnet', '.*Song Started'),
            (self.deviceB, 'playnet', '.*Song Started')
        ], 10)

        self.wait(2)

        self.head_call_A('app.webradio', 'play_station', WEBRADIO_B)
        self.head_call_B('app.pandora', 'play_station', PANDORA_B)

        self.expect_loglines([
            (self.deviceA, 'playnet', '.*Song Started'),
            (self.deviceB, 'playnet', '.*Song Started'),
            (self.deviceC, 'playnet', '.*Song Started'),
            (self.deviceD, 'playnet', '.*Song Started')
        ])

        self.wait(2)

        self.head_call_A('audio', 'pause')
        self.head_call_B('audio', 'pause')

        self.expect_loglines([
            (self.deviceA, 'distributor', '.*pause complete. Success: yes'),
            (self.deviceB, 'distributor', '.*pause complete. Success: yes'),
            (self.deviceC, 'audio.output', '.*pause'),
            (self.deviceD, 'audio.output', '.*pause')
        ])

        self.wait(2)

        self.head_call_A('audio', 'resume')
        self.head_call_B('audio', 'resume')

        self.expect_loglines([
            (self.deviceA, 'distributor', '.*resume complete. Success: yes'),
            (self.deviceB, 'distributor', '.*resume complete. Success: yes'),
            (self.deviceC, 'audio.output', '.*resume'),
            (self.deviceD, 'audio.output', '.*resume')
        ])

    ###### END TESTS ######

    def setUp(self):
        self.deviceA = Device('test.B', 32002)
        self.deviceA.start()
        self.wait(STD_WAIT)
        self.deviceB = Device('test.D', 32004)
        self.deviceB.start()
        self.wait(STD_WAIT)
        self.deviceC = Device('test.C', 32003)
        self.deviceC.start()
        self.wait(STD_WAIT)
        self.deviceD = Device('test.E', 32005)
        self.deviceD.start()
        self.wait(STD_WAIT)

    def tearDown(self):
        self.deviceA.close()
        self.deviceA.save_log(self.id())
        self.deviceB.close()
        self.deviceB.save_log(self.id())
        self.deviceC.close()
        self.deviceC.save_log(self.id())
        self.deviceD.close()
        self.deviceD.save_log(self.id())

    def boom_call_A(self, obj, method, params=None):
        self.deviceA.boom_call(obj, method, params)

    def boom_call_B(self, obj, method, params=None):
        self.deviceB.boom_call(obj, method, params)

    def head_call_A(self,obj,method,params=None):
        self.deviceA.head_call(obj, method, params)

    def head_call_B(self,obj,method,params=None):
        self.deviceB.head_call(obj, method, params)

    def expect_logline(self, device, obj, expr, timeout=STD_TIMEOUT):
        self.assertTrue(_expect_logline([device, obj, expr], timeout))

    def expect_loglines(self, scan_list, timeout=STD_TIMEOUT):
        if not ((type(scan_list) is list) or (type(scan_list) is tuple)):
            raise Exception('Must provide expect_loglines with a list or '
                    'tuple of (dev, obj, scan) tuples')
        self.assertTrue(_expect_loglines(scan_list, timeout))

    def wait(self, delay=STD_WAIT):
        time.sleep(delay)

class TwoMasterTest(unittest.TestCase):
    def test_audio_apps_http(self):
        self.expect_loglines([
            (self.deviceA, 'beephead', '.*Update state:.*app\.pandora'),
            (self.deviceA, 'beephead', '.*Update state:.*app\.webradio'),
            (self.deviceB, 'beephead', '.*Update state:.*app\.pandora'),
            (self.deviceB, 'beephead', '.*Update state:.*app\.webradio')
        ], 10)

        self.head_call_A('app.pandora', 'play_station', PANDORA_A)
        self.head_call_B('app.webradio', 'play_station', WEBRADIO_A)

        self.expect_loglines([
            (self.deviceA, 'playnet', '.*Song Started'),
            (self.deviceB, 'playnet', '.*Song Started')
        ], 10)

        self.head_call_A('app.webradio', 'play_station', WEBRADIO_B)
        self.head_call_B('app.pandora', 'play_station', PANDORA_B)

        self.expect_loglines([
            (self.deviceA, 'playnet', '.*Song Started'),
            (self.deviceB, 'playnet', '.*Song Started')
        ])

        self.head_call_A('audio', 'pause')
        self.head_call_B('audio', 'pause')

        self.expect_loglines([
            (self.deviceA, 'distributor', '.*pause complete. Success: yes'),
            (self.deviceB, 'distributor', '.*pause complete. Success: yes')
        ])

        self.head_call_A('audio', 'resume')
        self.head_call_B('audio', 'resume')

        self.expect_loglines([
            (self.deviceA, 'distributor', '.*resume complete. Success: yes'),
            (self.deviceB, 'distributor', '.*resume complete. Success: yes')
        ])

    ###### END TESTS ######

    def setUp(self):
        self.deviceA = Device('test.A', 32001)
        self.deviceA.start()
        self.wait(3)
        self.deviceB = Device('test.B', 32002)
        self.deviceB.start()
        self.wait(10)

    def tearDown(self):
        self.deviceA.close()
        self.deviceA.save_log(self.id())
        self.deviceB.close()
        self.deviceB.save_log(self.id())

    def boom_call_A(self, obj, method, params=None):
        self.deviceA.boom_call(obj, method, params)

    def boom_call_B(self, obj, method, params=None):
        self.deviceB.boom_call(obj, method, params)

    def head_call_A(self,obj,method,params=None):
        self.deviceA.head_call(obj, method, params)

    def head_call_B(self,obj,method,params=None):
        self.deviceB.head_call(obj, method, params)

    def expect_logline(self, device, obj, expr, timeout=STD_TIMEOUT):
        self.assertTrue(_expect_logline([device, obj, expr], timeout))

    def expect_loglines(self, scan_list, timeout=STD_TIMEOUT):
        if not ((type(scan_list) is list) or (type(scan_list) is tuple)):
            raise Exception('Must provide expect_loglines with a list or '
                    'tuple of (dev, obj, scan) tuples')
        self.assertTrue(_expect_loglines(scan_list, timeout))

    def wait(self, delay=STD_WAIT):
        time.sleep(delay)

class SingleMasterTest(unittest.TestCase):
    def test_audio_apps_ubus(self):
        # Wait for both pandora and webradio
        self.expect_loglines(
                [('beephead', '.*Update state:.*app\.pandora'),
                 ('beephead', '.*Update state:.*app\.webradio')])

        # Play a station on pandora
        self.boom_call('beep.app.pandora', 'play_station', PANDORA_A)
        self.expect_logline('playnet', '.*Song Started')

        # Play a station on webradio
        self.boom_call('beep.app.webradio', 'play_station', WEBRADIO_A)
        self.expect_logline('playnet', '.*Song Started')

        # Pause
        self.boom_call('beep.distributor', 'pause')
        self.expect_logline('distributor', '.*pause complete. Success: yes')

        # Resume
        self.boom_call('beep.distributor', 'resume')
        self.expect_logline('distributor', '.*resume complete. Success: yes')

    def test_audio_apps_http(self):
        # Wait for both pandora and webradio
        self.expect_loglines(
                [('beephead', '.*Update state:.*app\.pandora'),
                 ('beephead', '.*Update state:.*app\.webradio')])

        # Play a station on webradio
        self.head_call('app.webradio', 'play_station', WEBRADIO_A)
        self.expect_logline('playnet', '.*Song Started')

        # Play a station on pandora
        self.head_call('app.pandora', 'play_station', PANDORA_A)
        self.expect_logline('playnet', '.*Song Started')

        # Pause
        self.head_call('audio', 'pause')
        self.expect_logline('distributor', '.*pause complete. Success: yes')

        # Resume
        self.head_call('audio', 'resume')
        self.expect_logline('distributor', '.*resume complete. Success: yes')

        # Skip
        self.head_call('audio', 'skip')
        self.expect_logline('app_pandora', '.*Pandora track started')

    ###### END TESTS ######

    def setUp(self):
        self.device = Device('test.A', 32001)
        self.device.start()
        self.wait(1)

    def tearDown(self):
        self.device.close()
        self.device.save_log(self.id())

    def boom_call(self, obj, method, params=None):
        self.device.boom_call(obj, method, params)

    def head_call(self,obj,method,params=None):
        self.device.head_call(obj, method, params)

    def expect_logline(self, obj, expr, timeout=STD_TIMEOUT):
        self.assertTrue(_expect_loglines([(self.device, obj, expr)], timeout))

    def expect_loglines(self, scan_list, timeout=STD_TIMEOUT):
        if not ((type(scan_list) is list) or (type(scan_list) is tuple)):
            raise Exception('Must provide expect_loglines with a list or '
                    'tuple of (obj, scan) tuples')
        _scan_list = []
        for item in scan_list:
           _scan_list.append((self.device, item[0], item[1]))
        self.assertTrue(_expect_loglines(_scan_list, timeout))

    def wait(self, delay=STD_WAIT):
        time.sleep(delay)

class Device:
    def chdir(self):
        os.chdir(HOME_DIR + '/' + self.path)

    def log_path(self):
        return './beepmanager.log'

    def start(self):
        self.chdir()
        subprocess.call(['boom', 'start'],stdout=dev_null,stderr=dev_null)
        self.fd = open(self.log_path())

    def consume_old(self):
        self.fd.read()

    def __init__(self,path, http_port):
        self.path = path
        self.http_host = 'http://localhost:%d/synapse' % (http_port)

    def __enter__(self):
        return self

    def _docmd(self, cmd):
        self.chdir()
        args = shlex.split(cmd)
        subprocess.call(args,stdout=dev_null,stderr=dev_null)

    def boom_call(self,obj,method,params):
        cmd = 'boom ubus call %s %s' % \
            (obj, method)
        if params:
            cmd = '%s \'%s\'' % (cmd, json.dumps(params))

        self._docmd(cmd)

    # Avert thy eyes:  This is hack country
    def _init_context(self):
        cmd = 'boom ubus call beep.manager get'
        ident = \
            ' '.join(subprocess
                .check_output(shlex.split(cmd))
                .split())
        ident_dict = json.loads(re.search('\{.*\}$', ident).group(0))
        masters = ident_dict['result']['masters']
        for (context, data) in masters.items():
            if data['device_name'] == self.path:
                self.context = 'group.%s' % context
                return
        raise Exception('Test group not found')

    def head_call(self,obj,method,params):
        if not hasattr(self, 'context'):
            self._init_context()

        if not params:
            params = {}

        payload = {
            'jsonrpc' : '2.0',
            'method' : 'call',
            'params' : [
                'beep.head',
                'call', {
                    'context':self.context,
                    'object':obj,
                    'method':method,
                    'params':params
                }
            ]
        }

        thread.start_new_thread(requests.post,
                (self.http_host, json.dumps(payload)))

    def save_log(self, id):
        self.chdir()
        shutil.copyfile(
                self.log_path(),
                '/tmp/%s.%s.beeptestlog' % (self.path, id))

    def close(self):
        global dev_null
        self.chdir()
        subprocess.call(['boom', 'stop'],stdout=dev_null,stderr=dev_null)
        time.sleep(.5)
        self.fd.close()

    def __exit__(self, type, value, traceback):
        self.close()

def _expect_loglines(scan_list, timeout=STD_TIMEOUT):
    now = time.time()
    found = {}
    scan_devices = {}
    for (dev, obj, scan) in scan_list:
        if not dev in scan_devices:
            scan_devices[dev] = {}
        pstr = '%s%s' % (obj, scan)
        found_key = '%s:%s' % (dev.path, pstr)
        found[found_key] = False
        scan_devices[dev][pstr] = re.compile(pstr)
    while(time.time() - now <= timeout):
        did_read = True
        for (device, patterns) in scan_devices.items():
            line = device.fd.readline()
            if not line:
                did_read = False
            else:
                for (expr, pat) in patterns.items():
                    if pat.search(line):
                        found['%s:%s' % (device.path, expr)] = True
        if not did_read:
            time.sleep(.01)
        if all(found.values()):
            return True
    return False

if __name__ == "__main__":
    dev_null = open(os.devnull, "w")
    unittest.main()
    dev_null.close()
