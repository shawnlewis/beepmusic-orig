import time

from beep import interface
import tests.core

def get_evil_devs(dtab):
    evil = {}
    good = {}
    for dev_id, dev in dtab.iteritems():
        if dev.evil_interface:
            evil[dev_id] = dev
        else:
            good[dev_id] = dev
    return evil, good


class TestAudioNetworkDropout(tests.core.BeepTest):
    name = 'Evil audio test (dropout)'

    def test(self, drop_time=5, devs_to_drop=1,
             expect_remain_in_group=True,
             expect_audio_after=True):
        evil_devs, good_devs = get_evil_devs(self.dtab)
        assert devs_to_drop <= len(evil_devs), 'Don\'t have enough evil devs'

        non_drop_devs = good_devs
        drop_devs = {}
        for i, (dev_id, dev) in enumerate(evil_devs.iteritems()):
            if i < devs_to_drop:
                drop_devs[dev_id] = dev
            else:
                non_drop_devs[dev_id] = dev

        self.logc('Dropout devs: %s' % drop_devs.keys())
        self.logc('Non-Dropout devs: %s' % non_drop_devs.keys())

        tests.core.reset(self.dtab)

        dev0 = good_devs.values()[0]

        groups = tests.core.wait_for_groups(self.dtab, {'1': self.dtab.keys()})
        self.logc('Got groups: %s' % groups, 1)
        group_id = groups.keys()[0]
        group_state = groups[group_id]

        # start audio on all groups
        self.logc('Starting audio, using head: %s' % dev0.host, 1)
        dev0.head_call(
                'group.' + group_id,
                'app.webradio',
                'play_station',
                {'url': 'http://ice.somafm.com/bootliquor',
                 'name': 'SomaFM: Boot Liquor'})

        # wait for audio state playing
        self.loga('Verifying audio state playing', 1)
        dev = self.dtab[group_state['master']]
        dev.wait_for_state(
                'beep.distributor',
                'audio_state',
                'playing')

        # check that played time is now advancing on all devices
        self.loga('Verifying played time is advancing on all devices', 1)
        played_times = {}
        for dev_id, dev in self.dtab.iteritems():
            played_times[dev_id] = dev.get_state(
                    'beep.playnet')['written_track_time']
        time.sleep(2)
        for dev_id, dev in self.dtab.iteritems():
            dev.wait_for_state('beep.playnet', 'written_track_time',
                    played_times[dev_id], test=lambda v,o: v > o)

        self.logc('Network loss to 100% on drop_devs')
        for dev_id, dev in drop_devs.iteritems():
            dev.set_loss(loss_percent=100)

        self.logc('Waiting %ss' % drop_time)
        time.sleep(drop_time)

        # TODO: check that audio is still playing during dropout (only will
        #     if we had enough buffer when it happened though)

        # TODO: check here for dropout

        self.logc('Restoring network on drop_devs')
        for dev_id, dev in drop_devs.iteritems():
            dev.set_loss(loss_percent=0)

        # some time after the network comes back the devices should come back
        time.sleep(10)

        groups = tests.core.wait_for_groups(self.dtab, {'1': self.dtab.keys()})
        self.logc('Got groups: %s' % groups, 1)
        group_id = groups.keys()[0]
        group_state = groups[group_id]

        # TODO: check that distributor has the same devices as the group

        # check that we can do a head call on the disconnected devices,
        # this ensures urelay is still connected
        for dev_id, dev in drop_devs.iteritems():
            result = interface.head_call(
                    dev.host, 'group.%s' % group_id, 'manager', 'get_state')
            assert result['success'], (
                    'headcall error: %s' % result.get('error_message'))

        if expect_audio_after:
            # check that played time is still advancing on all devices
            self.loga('Verifying played time is advancing on all devices', 1)
            played_times = {}
            for dev_id, dev in self.dtab.iteritems():
                played_times[dev_id] = dev.get_state(
                        'beep.playnet')['written_track_time']
            time.sleep(2)
            for dev_id, dev in self.dtab.iteritems():
                dev.wait_for_state('beep.playnet', 'written_track_time',
                        played_times[dev_id], test=lambda v,o: v > o)
