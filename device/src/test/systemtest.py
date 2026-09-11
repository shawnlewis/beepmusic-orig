#!/usr/bin/python
import subprocess, shlex, os, re, time, unittest, json, requests, thread
import shutil, random, sys

STD_TIMEOUT = 10 # seconds
STD_WAIT = 10 # seconds
# CMD_INTERVAL_RANGE = (1,120)
CMD_INTERVAL_RANGE = (1,10)

HOME_DIR = os.getcwd()

# PANDORA_A = {'id':'19566453720794262'}
# PANDORA_B = {'id':'77277697348912278'}

WEBRADIO_A = {
    'url':'http://ice.somafm.com/groovesalad',
    'name':'SomaFM GrooveSalad' }
WEBRADIO_B = {
    'url':'http://ice.somafm.com/dubstep',
    'name':'SomaFM Dubstep Beyond' }

device_bank = {
    'test.A':{
        'uhttpd_port':32001,
        'desc':'A: master (0 slaves)'
    },
    'test.B':{
        'uhttpd_port':32002,
        'desc':'B: master (1 slave)'
    },
    'test.C':{
        'uhttpd_port':32003,
        'desc':'C: slave 1/1 to B'
    },
    'test.D':{
        'uhttpd_port':32004,
        'desc':'D: master (2 slaves)'
    },
    'test.E':{
        'uhttpd_port':32005,
        'desc':'E: slave 1/2 to D'
    },
    'test.F':{
        'uhttpd_port':32006,
        'desc':'F: slave 2/2 to D'
    },
    'test.G':{
        'uhttpd_port':32007,
        'desc':'G: master (3 slaves)'
    },
    'test.H':{
        'uhttpd_port':32008,
        'desc':'H: slave 1/3 to G'
    },
    'test.I':{
        'uhttpd_port':32009,
        'desc':'I: slave 2/3 to G'
    },
    'test.J':{
        'uhttpd_port':32010,
        'desc':'J: slave 3/3 to G'
    }
}

devices = {}

class Command:
    def __init__(self, desc, cmd, args=None):
        self.desc = desc
        self.cmd = cmd
        self.args = args

    def _is_compound(self):
        return (type(self.cmd) is list) or (type(self.cmd) is tuple)
            
    def do_cmd(self):
        if self.args:
            self.cmd(self.desc, *self.args)
        else:
            self.cmd(self.desc)

class Device:
    def chdir(self):
        os.chdir(HOME_DIR + '/' + self.path)

    def start(self):
        global dev_null
        self.chdir()
        subprocess.call(['boom', 'start'],
                stdout=dev_null,
                stderr=dev_null)
        self._init_metadata()

    def __init__(self,path, http_port, desc):
        self.path = path
        self.desc = desc
        self.http_host = 'http://localhost:%d/synapse' % (http_port)
        self.start()

    def __enter__(self):
        return self

    def _docmd(self, cmd):
        global dev_null
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
    def _init_metadata(self):
        cmd = 'boom ubus call beep.manager get'
        ident = \
            ' '.join(subprocess
                .check_output(shlex.split(cmd))
                .split())
        ident_dict = json.loads(re.search('\{.*\}$', ident).group(0))

        self.sink_id = ident_dict['result']['sink_id']
        self.source_id = ident_dict['result']['source_id']
        self.is_master = ident_dict['result']['is_master']
        self.device_id = ident_dict['result']['device_id']
        self.device_name = ident_dict['result']['device_name']

    def _init_context(self):
        cmd = 'boom ubus call beep.manager get'
        ident = \
            ' '.join(subprocess
                .check_output(shlex.split(cmd))
                .split())
        ident_dict = json.loads(re.search('\{.*\}$', ident).group(0))

        masters = ident_dict['result']['masters']

        if masters:
            for (context, data) in masters.items():
                if data['source_id'] == self.sink_id:
                    self.context = 'group.%s' % context
                    return
            raise Exception('Test group not found')

    def get_context(self):
        if not hasattr(self, 'context'):
            self._init_context()

        return self.context

    def head_call(self,obj,method,params):
        if not params:
            params = {}

        payload = {
            'jsonrpc' : '2.0',
            'method' : 'call',
            'params' : [
                'beep.head',
                'call', {
                    'context':self.get_context(),
                    'object':obj,
                    'method':method,
                    'params':params
                }
            ]
        }

        thread.start_new_thread(requests.post,
                (self.http_host, json.dumps(payload)))

    def close(self):
        self._docmd('boom stop')
        if hasattr(self, 'context'):
            del self.context
        time.sleep(.5)

    def __exit__(self, type, value, traceback):
        self.close()


ANY_DEVICE = '__any_device__'
ANY_MASTER = '__any_master__'
ANY_HOST = '__any_host__'
ANY_CONTEXT = '__any_context__'

def log_cmd(desc, target):
    print time.strftime("%H:%M:%S") + ' ' + desc + \
        ' --> ' + target

def cycle_dev(desc, device):
    if device == ANY_DEVICE:
        _device_key = random.choice(devices.keys())
    elif device == ANY_MASTER:
        _device_key = random.choice(devices.keys())
        while not devices[_device_key].is_master:
            _device_key = random.choice(devices.keys())
    else:
        _device_key = device

    log_cmd(desc, devices[_device_key].desc)
    devices[_device_key].close()
    time.sleep(5)
    devices[_device_key].start()
    

def call_dev_http(desc, device, obj, method, params=None):
    if device == ANY_DEVICE:
        _device_key = random.choice(devices.keys())
    elif device == ANY_MASTER:
        _device_key = random.choice(devices.keys())
        while not devices[_device_key].is_master:
            _device_key = random.choice(devices.keys())
    else:
        _device_key = device
    
    log_cmd(desc, devices[_device_key].desc)
    devices[_device_key].head_call(obj, method, params)

def raw_http_cmd(desc, host, context, obj, method, params=None, target=None):
    if host == ANY_HOST:
        _device_key = random.choice(devices.keys())
        _host = devices[_device_key].http_host
    else:
        _host = host

    if context == ANY_CONTEXT:
        _device_key = random.choice(devices.keys())
        _context = devices[_device_key].get_context()
    else:
        _context = context

    if not params:
        params = {}

    payload = {
        'jsonrpc' : '2.0',
        'method' : 'call',
        'params' : [
            'beep.head',
            'call', {
                'context':_context,
                'object':obj,
                'method':method,
                'params':params
            }
        ]
    }

    if not target:
        log_cmd(desc, devices[_device_key].device_name)
    else:
        log_cmd(desc, target)

    #thread.start_new_thread(requests.post,
    #    (_host, json.dumps(payload)))
    requests.post(_host, json.dumps(payload))

def volume_http(desc):
    _device_key = random.choice(devices.keys())
    dev = devices[_device_key]

    for i in range(0,20):
        raw_http_cmd(desc, ANY_HOST, dev.get_context(),
                'audio', 'set_volume',
                {'players':{dev.device_id:random.randint(1,1000)}},
                target=dev.desc)
        time.sleep(0.1)

# NOT SANITIZED PLEASE BE CAREFUL
def meta_call(cmd):
    args = shlex.split(cmd)
    subprocess.call(args,stdout=dev_null,stderr=dev_null)

# Command definitions

commands = [
#     # Pandora-related 
#     Command('Play pandora station #1',
#         call_dev_http,
#         (ANY_MASTER, 'app.pandora', 'play_station', PANDORA_A)
#     ),
#     Command('Play pandora station #2',
#         call_dev_http,
#         (ANY_MASTER, 'app.pandora', 'play_station', PANDORA_B)
#     ),

    # Cycle a device
    Command('Cycle device',
        cycle_dev,
        (ANY_DEVICE,)
    ),

    # Webradio-related
    Command('Play webradio station #1',
        call_dev_http,
        (ANY_MASTER, 'app.webradio', 'play_station', WEBRADIO_A)
    ),
    Command('Play webradio station #2',
        call_dev_http,
        (ANY_MASTER, 'app.webradio', 'play_station', WEBRADIO_B)
    ),

    # Audio stream 
    Command('Resume audio via HTTP interface',
        call_dev_http,(ANY_MASTER, 'audio', 'resume')
    ),
    Command('Pause audio via HTTP interface',
        call_dev_http,(ANY_MASTER, 'audio', 'pause')
    ),
    Command('Skip via HTTP interface',
        call_dev_http,(ANY_MASTER, 'audio', 'skip')
    ),

    # Volume
    Command('Random set volume command',volume_http)
]


if __name__ == "__main__":
    dev_null = open(os.devnull, 'w')
    for k,v in device_bank.items():
        d = Device(k,v['uhttpd_port'],v['desc'])
        devices[k] = d

    time.sleep(5)
    print 'Entering main loop...'
    while True:
        wait_time = random.randint(*CMD_INTERVAL_RANGE)
        while wait_time > 0:
            wait_str = 'Waiting %.2d seconds...' % wait_time
            sys.stdout.write(wait_str)
            sys.stdout.flush()
            time.sleep(1)
            sys.stdout.write('\r' + ' '*len(wait_str) + '\r')
            sys.stdout.flush()
            wait_time = wait_time - 1

        next_cmd = random.choice(commands)
        next_cmd.do_cmd()

    dev_null.close()
