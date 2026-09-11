#!/usr/bin/python

import socket
import sys
import traceback
import urllib2

import beep_interface
import testcore
import test_audio
import test_hammer

CAMPFIRE_HEADERS = {
    'Content-Type': 'application/xml'
}
CAMPFIRE_DATA = '<message><body>{}</body></message>'
CAMPFIRE_INFO = {
    'group': 'beepdevices',
    'room': '570355',
    'auth': '88671ea5695cbaa6c4a1596d04c4e60d6faa901b:X'
}

# stolen from cloud/ansible/files/logstash/logstash-report.py
# TODO: merge all our Python stuff.
def alert_campfire(cf, msg):
    user, passwd = cf['auth'].split(':')
    url = 'https://{}.campfirenow.com/room/{}/speak.xml'.format(cf['group'],
            cf['room'])
    msg = msg.replace('<', '&lt;').replace('>', '&gt;')
    passmgr = urllib2.HTTPPasswordMgrWithDefaultRealm()
    passmgr.add_password(None, url, user, passwd)
    authhdl = urllib2.HTTPBasicAuthHandler(passmgr)
    opener = urllib2.build_opener(authhdl)
    urllib2.install_opener(opener)
    req = urllib2.Request(url, CAMPFIRE_DATA.format(msg), CAMPFIRE_HEADERS)
    try:
        res = urllib2.urlopen(req)
    except:
        pass

def run_test(test, params=None):
    testcore._log('Running test: %s' % test.name)
    if params is None:
        params = {}
    try:
        test.run(**params)
    except AssertionError:
        testcore._log('Test encountered assertion: %s' % test.name)
        traceback.print_exc()
        alert_campfire(
                CAMPFIRE_INFO,
                'Test runner running on %s. Test failed: %s.\nDevices have been left in their last state for inspection.' % (socket.gethostname(), test.name))
        testcore._log('Exiting')
        sys.exit(1)
    except KeyboardInterrupt:
        traceback.print_exc()
        sys.exit(0)
    except:
        testcore._log('Test encountered exception: %s' % test.name)
        traceback.print_exc()
    else:
        testcore._log('Test passed: %s' % test.name)


def safe_reset():
    try:
        testcore.reset(beep_interface.dtab)
    except:
        testcore._log('Reset encountered exception: %s' % test.name)
        traceback.print_exc()
        return False
    return True


def main(argv):

    # always disable spotify
    for dev_id, dev in beep_interface.dtab.iteritems():
        dev.uci_set('devel', 'disable_app_spotify', 1)

    while True:

        # test_audio
        if safe_reset():
            t = test_audio.AudioTest(beep_interface.dtab)
            run_test(t)

        # test_hammer
        if safe_reset():
            t = test_hammer.HammerTest(beep_interface.dtab)
            run_test(t, {'total_duration': 5, 'hammer_duration': 60})


if __name__ == '__main__':
    main(sys.argv)
