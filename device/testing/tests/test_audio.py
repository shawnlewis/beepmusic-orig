import time, tests.core

class AudioTest(tests.core.BeepTest):
    name = 'Audio Test'

    def test(self):
        tests.core.reset(self.dtab)

        # TODO: we shouldn't rely on external webradio for this, unless we're
        # willing to wait longer in case of slow buffering.

        dev0 = self.dtab.values()[0]
        groups = tests.core.wait_for_groups(self.dtab, {'1': self.dtab.keys()})
        self.logc('Got groups: %s' % groups, 1)

        # start audio on all groups
        self.logc('Starting audio on all groups, using head: %s' % dev0.host, 1)
        for group_id in groups:
            dev0.head_call(
                    'group.' + group_id,
                    'app.webradio',
                    'play_station',
                    {'url': 'http://ice.somafm.com/bootliquor',
                     'name': 'SomaFM: Boot Liquor'})

        # wait for audio state playing
        self.loga('Verifying audio state playing on all groups', 1)
        for group_id, group_state in groups.iteritems():
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

        # pause all devices
        self.logc('Pausing all groups', 1)
        for group_id, dev_ids in groups.iteritems():
            dev = self.dtab[group_state['master']]
            dev.head_call(
                    'group.' + group_id,
                    'audio',
                    'pause',
                    {})

        # wait for audio state paused
        self.loga('Verifying audio state paused on all groups', 1)
        for group_id, group_state in groups.iteritems():
            dev = self.dtab[group_state['master']]
            dev.wait_for_state(
                    'beep.distributor',
                    'audio_state',
                    'paused')

        # check that played time is no longer advancing on all devices
        self.loga('Verifying played time is not advancing on all devices', 1)

        all_stopped = False
        not_stopped = None
        iters = 10
        while not all_stopped and iters:
            played_times = {}
            for dev_id, dev in self.dtab.iteritems():
                played_times[dev_id] = dev.get_state(
                        'beep.playnet')['written_track_time']
            time.sleep(1)
            all_stopped = True
            for dev_id, dev in self.dtab.iteritems():
                if dev.get_state('beep.playnet')['written_track_time'] \
                        != played_times[dev_id]:
                    all_stopped = False
                    not_stopped = dev.host
                    break
            iters -= 1
        assert(all_stopped), 'Audio not stopped on at least: %s' % not_stopped

        # resume all groups
        self.logc('Resuming all groups', 1)
        for group_id, dev_ids in groups.iteritems():
            dev0.head_call(
                    'group.' + group_id,
                    'audio',
                    'resume',
                    {})

        # wait for audio state playing
        self.loga('Verifying audio state playing on all groups', 1)
        for group_id, group_state in groups.iteritems():
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
        time.sleep(1)
        for dev_id, dev in self.dtab.iteritems():
            assert(dev.get_state(
                'beep.playnet')['written_track_time'] > played_times[dev_id])
