#!/usr/bin/env python

import unittest
from tests import test_audio
from tests import test_evil
from tests import test_cycle_services
from tests import test_grouping
from tests import test_joining

import testrack
dtab = testrack.dtab

class BeepTest(unittest.TestCase):
    def test_cycle_services(self):
        test_cycle_services.CycleServicesTest(dtab).run()

    def test_evil_short_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(drop_time=5)

    def test_evil_10_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=10, expect_audio_after=False)

    def test_evil_15_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=15, expect_audio_after=False)

    def test_evil_20_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=20, expect_audio_after=False)

    def test_evil_25_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=25, expect_audio_after=False)

    def test_evil_30_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=30, expect_audio_after=False)

    def test_evil_long_dropout_with_audio(self):
        test_evil.TestAudioNetworkDropout(dtab).run(
                drop_time=35, expect_audio_after=False)

    #def test_evil_really_long_dropout_with_audio(self):
    #    test_evil.TestAudioNetworkDropout(dtab).run(
    #            drop_time=700, expect_audio_after=False)

    def test_audio(self):
        test_audio.AudioTest(dtab).run()

    def test_grouping(self):
        test_grouping.GroupingTest(dtab).run(rep=5)

    def test_rapid_grouping(self):
        test_grouping.RapidGroupingTest(dtab).run(rep=10)

    def test_stream_join_webradio(self):
        test_joining.JoiningTest(dtab).run(app='webradio', duration_s=300)

if __name__ == '__main__':
    unittest.main()
