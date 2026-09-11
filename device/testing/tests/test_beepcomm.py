import tests.core, random, time, re, socket

from beep.dial import DialDiscovery, DialInterface
from beep.castchat import *

class DialTest(tests.core.BeepTest):
    name = "Beepcomm/Dial Test"

    def test(self):
        # Stop all, start only one
        tests.core.stop_all(self.dtab)
        dev_id = random.sample(self.dtab, 1)[0]
        self.log('Selected %s' % dev_id)
        dev = self.dtab[dev_id]

        dev.start()
        tests.core.check_base(dev.dtab())

        dev_name = dev.get_state_field('beep.manager','local_device name')

        time.sleep(2)
        discovery = DialDiscovery()
        ssdp_responses = discovery.discover_all()

        ssdp_response = None
        for r in ssdp_responses:
            if r['friendly_name'] == dev_name:
                ssdp_response = r
                break

        # beepcomm is discoverable and dd.xml contains correct response
        assert ssdp_response != None, \
                "ssdp response not found or friendly_name != %s" % dev_name
        assert ssdp_response['model_name'] == 'Beep Model 001', \
                "ssdp response: model_name != Beep Model 001"
        assert len(ssdp_response['udn']) == 41, \
                "ssdp response: udn length != 41 (%d)" % len(ssdp_response['udn'])
        assert ssdp_response['url'].startswith('http://'), \
                "ssdp response: apps url missing http:// prefix"
        assert ssdp_response['url'].endswith('/apps/'), \
                "ssdp response: apps url missing /apps/ suffix"

        # echo app
        iface = DialInterface(ssdp_response['url'])

        prev_session_id = -1

        COMMAND_DELAY = 0.5
        ITERATION_DELAY = 1.0

        for x in xrange(10):
            self.logc('App lifecycle test #%d' % x)
            # Start (do it three times, expect same result)
            for y in xrange(3):
                #self.logc('App start #%d/%d' % (x,y))
                r = iface.start('echo')
                assert r[0] == 201, \
                        "apps: start status code != 201"
                assert r[1].endswith('/apps/echo/run'), \
                        "apps: instance url does not end with /apps/echo/run"
                time.sleep(COMMAND_DELAY)

            # Info (Started)
            #self.logc('App info #%d' % (x))
            r = iface.info('echo')
            assert r[0] == 200, \
                    "apps: (started)info status code != 200"
            assert r[1].find('<name>echo</name>') != -1, \
                    "apps: (started)info response missing name element"
            assert r[1].find('<options allowStop="true"/>') != -1, \
                    "apps: (started)info response missing options element"
            assert r[1].find('<appdata xmlns="urn:castchat-org:device:app">') != -1, \
                    "apps: (started)info response missing appdata element"
            assert r[1].find('<state>running</state>') != -1, \
                    "apps: (started)info response missing state element"
            assert r[1].find('<link rel="run" href="run"/>') != -1, \
                    "apps: (started)info response missing link element"

            match = re.search('<message-url>(.*)</message-url>', r[1])

            assert match != None, \
                    "apps: (started)info response missing message-url element"
            assert match.groups()[0].startswith('ws://'), \
                    "apps: (started)message-url missing ws:// prefix"
            assert match.groups()[0].endswith('/castchat/echo'), \
                    "apps: (started)message-url missing /castchat/echo suffix"

            match = re.search('<session-id>(\d*)</session-id>', r[1])

            assert match != None, \
                    "apps: (started)info response missing session-id element"

            try:
                session_id = int(match.groups()[0])
                assert session_id > prev_session_id, \
                        "apps: (started)session_id not greater than previous session_id"
                prev_session_id = session_id
            except ValueError:
                raise AssertionError("apps: (started)session_id is non-numeric")

            # Stop (do it three times, expect different result for 1st then subsequent...)
            for y in xrange(3):
                #self.logc('App stop #%d/%d' % (x,y))
                r = iface.stop('echo/run')
                if y < 1: # Iter 1
                    assert r[0] == 200, \
                            "apps: stop status code != 200"
                    # Response body for stop is undefined, but should be empty
                    assert r[1] == '', \
                            "apps: stop response is not empty"
                else:
                    assert r[0] == 404, \
                            "apps: stop status code for non-running app != 404"
                    assert r[1] == 'Error 404: Not Found\r\n', \
                            "apps: stop response for non-running app is invalid"
                time.sleep(COMMAND_DELAY)

            # Info (Stopped)
            r = iface.info('echo')
            assert r[0] == 200, \
                    "apps: (stopped) info status code != 200"
            assert r[1].find('<name>echo</name>') != -1, \
                    "apps: (stopped) info response missing name element"
            assert r[1].find('<options allowStop="true"/>') != -1, \
                    "apps: (stopped) info response missing options element"
            assert r[1].find('<appdata xmlns="urn:castchat-org:device:app">') != -1, \
                    "apps: (stopped) info response missing appdata element"
            assert r[1].find('<state>stopped</state>') != -1, \
                    "apps: (stopped) info response missing state element"

            match = re.search('<message-url>(.*)</message-url>', r[1])

            assert match != None, \
                    "apps: (stopped) info response missing message-url element"
            assert match.groups()[0].startswith('ws://'), \
                    "apps: (stopped) message-url missing ws:// prefix"
            assert match.groups()[0].endswith('/castchat/echo'), \
                    "apps: (stopped) message-url missing /castchat/echo suffix"

            match = re.search('<session-id>(\d*)</session-id>', r[1])

            assert match != None, \
                    "apps: (stopped) info response missing session-id element"

            try:
                session_id = int(match.groups()[0])
                assert session_id == 0, \
                        "apps: (stopped) session_id not zero"
            except ValueError:
                raise AssertionError("apps: (stopped) session_id is non-numeric")

            time.sleep(ITERATION_DELAY)

from string import ascii_letters, digits, punctuation

_chars = ascii_letters + digits + punctuation
def randstring(size):
    return ''.join(random.choice(_chars) for _ in xrange(size))

class CastchatTest(tests.core.BeepTest):
    name = "Beepcomm/Castchat Test"

    def test(self):
        # Stop all, start only one
        tests.core.stop_all(self.dtab)
        dev_id = random.sample(self.dtab, 1)[0]
        self.log('Selected %s' % dev_id)
        dev = self.dtab[dev_id]

        dev.start()
        tests.core.check_base(dev.dtab())

        dev_name = dev.get_state_field('beep.manager','local_device name')

        time.sleep(2)
        discovery = DialDiscovery()
        ssdp_responses = discovery.discover_all()

        ssdp_response = None
        for r in ssdp_responses:
            if r['friendly_name'] == dev_name:
                ssdp_response = r
                break

        iface = DialInterface(ssdp_response['url'])
        iface.start('echo')
        r = iface.info('echo')[1]

        match = re.search('<message-url>(.*)/echo</message-url>', r)
        url = match.groups()[0]

        clients = []
        N_CLIENTS = 7
        # Open 7 clients
        for x in xrange(N_CLIENTS):
            cc = CastChatClient(url,'echo')
            cc.open()
            cc.hello()
            clients.append(cc)

        # Once we've gotten to this point, we have 7 clients connected to
        # app_echo

        N_MESSAGES = 20

        for x in xrange(5):
            # Select random client
            cc = random.sample(clients, 1)[0]
            self.logc('Message spam')
            for y in xrange(N_MESSAGES):
                cc.send('echo','ECHO MESSAGE %d: %s' % (y, randstring(random.randint(0,500))))

            # app_echo waits a random interval between 0-3s, provide double
            # that interval to account for lag
            time.sleep(6)

            # We should receive N_CLIENTS * y echoes
            self.loga('Counting echoes')
            echoes = 0
            for cc2 in clients:
                while True:
                    msg = cc2.recv(False) # Non blocking
                    if not msg:
                        break

                    if not msg[1]:
                        continue

                    try:
                        _msg = parse_msg(msg[1])

                        if _msg['content'].startswith('ECHO MESSAGE'):
                            echoes += 1
                    except ValueError:
                        raise AssertionError("castchat: Received malformed message: %s" % msg[1])

            assert echoes == N_CLIENTS * N_MESSAGES, \
                    "castchat: Didn't receive the correct amount of echoes (Expected %d, got %d)" % \
                    (N_CLIENTS * N_MESSAGES, echoes)
