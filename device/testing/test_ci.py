#!/usr/bin/env python

import unittest
from tests import test_audio
from tests import test_cycle_services
from tests import test_grouping
from tests import test_joining
import devs

dtab = devs.generate_devices()

for dev_id, dev in dtab.iteritems():
    dev.uci_set('devel', 'dummy_audio_output', '1')
    dev.uci_commit()

class BeepTest(unittest.TestCase):
    def test_audio(self):
        test_audio.AudioTest(dtab).run()

    def test_grouping(self):
        test_grouping.GroupingTest(dtab).run(rep=5)

    def test_rapid_grouping(self):
        test_grouping.RapidGroupingTest(dtab).run(rep=10)

    def test_cycle_services(self):
        test_cycle_services.CycleServicesTest(dtab).run()

    def test_stream_join_webradio(self):
        test_joining.JoiningTest(dtab).run(app='webradio')

if __name__ == '__main__':
    unittest.main()
