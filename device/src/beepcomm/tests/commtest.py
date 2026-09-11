#!/usr/bin/env python

import unittest, time
from xml.etree import ElementTree
from beep.dial import DialDiscovery, DialInterface
from beep.interface import VirtDevice
from beep.messages import Messenger

def make_xml_find(namespace):
    def xml_find(root, name):
        return root.find('{%s}%s' % (namespace, name))
    return xml_find

class BeepcommTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._device = VirtDevice('localhost:30001','beepcomm_test')
        cls._device.start()
        time.sleep(4)

    @classmethod
    def tearDownClass(cls):
        cls._device.stop()
        time.sleep(1)

    def test_dial_discovery(self):
        """Search for beepcomm_test device and validate data"""
        dd = DialDiscovery()
        dd.search(3)
        time.sleep(1)
        dd.stop()

        devices = []
        while not dd.ssdp_responses.empty():
            devices.append(dd.ssdp_responses.get())

        n_discovered = 0
        for dev in devices:
            if dev['friendly_name'] == 'BEEPCOMM TEST -- DO NOT DISTURB':
                n_discovered = n_discovered + 1

                self.assertEquals(dev['model_name'],'Beep Model 001',
                        'Incorrect model name in DIAL SSDP response')
                self.assertEquals(dev['manufacturer'],'Beep, Inc.',
                        'Incorrect manufacturer in DIAL SSDP response')

        self.assertTrue(n_discovered >= 1, 'Insufficient DIAL SSDP responses')

    def test_dial_get_info_testtarget(self):
        """Get info on testtarget and verify valid response"""

        di = DialInterface('http://localhost:40523/apps')

        resp = di.info('testtarget')
        self.assertEquals(resp[0], 200, 'Incorrect status')

        self.assertTrue(resp[1], 'Empty response on')
        root = ElementTree.fromstring(resp[1])

        ns_beep_find = make_xml_find('urn:beep-com:device:app')
        ns_dial_find = make_xml_find('urn:dial-multiscreen-org:schemas:dial')

        self.assertTrue(ns_dial_find(root, 'name') is not None)
        self.assertTrue(ns_dial_find(root, 'options') is not None)
        self.assertTrue(ns_dial_find(root, 'state') is not None)
        # self.assertTrue(ns_dial_find(root, 'link') is not None)
        self.assertTrue(ns_beep_find(root, 'appdata') is not None)

        self.assertEquals('testtarget', ns_dial_find(root, 'name').text)
        self.assertIn('allowStop', ns_dial_find(root, 'options').attrib)
        self.assertEquals('true', ns_dial_find(root, 'options').attrib['allowStop'])
        self.assertRegexpMatches(ns_dial_find(root, 'state').text,
                'running|stopped')
        # self.assertIn('rel', ns_dial_find(root, 'link').attrib)
        # self.assertEquals('run', ns_dial_find(root, 'link').attrib['rel'])
        # self.assertIn('href', ns_dial_find(root, 'link').attrib)
        # self.assertEquals('run', ns_dial_find(root, 'link').attrib['href'])

        appdata = ns_beep_find(root, 'appdata')
        self.assertTrue(appdata is not None, 'appdata element not found in SSDP response')
        self.assertTrue(ns_beep_find(appdata, 'message-url') is not None)
        self.assertRegexpMatches(ns_beep_find(appdata, 'message-url').text,
                '.*?:\\d{1,5}')
        self.assertTrue(ns_beep_find(appdata, 'app-id') is not None)
        self.assertEquals('testtarget', ns_beep_find(appdata, 'app-id').text)

    def test_dial_get_info_invalid(self):
        """Get info on invalid app and verify correct response"""

        di = DialInterface('http://localhost:40523/apps')

        resp = di.info('invalid_app')
        self.assertEquals(resp[0], 404)

    def test_dial_launch_testtarget(self):
        """Launch testtarget and verify correct response"""

        di = DialInterface('http://localhost:40523/apps')

        # No parameters
        resp = di.start('testtarget')
        self.assertEquals(resp[0], 201, 'Incorrect status')
        self.assertRegexpMatches(resp[1],
                'http://.*?:\\d{1,5}/apps/testtarget/run')

        # With parameters
        resp = di.start('testtarget','{"test_parameter":"true"}')
        self.assertEquals(resp[0], 201, 'Incorrect status')
        self.assertRegexpMatches(resp[1],
                'http://.*?:\\d{1,5}/apps/testtarget/run')

    def test_dial_launch_invalid(self):
        """Launch invalid app and verify correct response"""

        di = DialInterface('http://localhost:40523/apps')

        resp = di.start('invalid_app')
        self.assertEquals(resp[0], 503)

        resp = di.start('invalid_app','{"test_parameter":"true"}')
        self.assertEquals(resp[0], 503)

    def test_beep_connect_testtarget(self):
        """Launch testtarget and establish a message connection"""

        di = DialInterface('http://localhost:40523/apps')
        di.start('testtarget')

        m = Messenger('localhost',40524)
        m.connect('testtarget')
        self.assertTrue(m.socket is not None)
        m.disconnect()

    def test_beep_send_msg_testtarget(self):
        """Launch testtarget, establish message connection, and send a message"""

        di = DialInterface('http://localhost:40523/apps')
        di.start('testtarget')

        m = Messenger('localhost',40524)
        m.connect('testtarget')

        m.send('Test message','test-namespace')
        m.disconnect()

    def test_beep_recv_msg_testtarget(self):
        """Launch testtarget, establish message connection, and receive a message within 5 seconds"""

        di = DialInterface('http://localhost:40523/apps')
        di.start('testtarget')

        m = Messenger('localhost',40524)
        m.connect('testtarget')

        # Command testtarget to send a message
        self._device.ubus_call('beep.app.testtarget','_send_message',
                {'message':'Test message','namespace':'test-namespace'})

        # Wait for message
        msg = m.messages.get(True, 5)

        m.disconnect()
        self.assertTrue(msg is not None)

        # Do other checks on message

if __name__ == '__main__':
    unittest.main()
