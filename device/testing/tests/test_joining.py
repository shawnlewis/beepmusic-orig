import random
import time
import json

import tests.core
import tests.pandora

def start_pandora_station(head_dev, group_id):
    head_dev.head_call(
            'group.' + group_id,
            'manager',
            'start_app',
            {'name': 'pandora'})

    time.sleep(5)

    head_dev.head_call(
            'group.' + group_id,
            'app.pandora',
            'msg_socket_sender_connected',
            {'sender_id': '1',
             'user_agent': 'beep_automated_test'})

    head_dev.head_call(
            'group.' + group_id,
            'app.pandora',
            'msg_socket_message_received',
            {'sender_id': '1',
             'namespace': 'urn:x-cast:com.google.cast.media',
             'message': json.dumps(tests.pandora.load_station1_message)})


class JoiningTest(tests.core.BeepTest):
    name = 'Stream Join Test'

    def test(self, app='webradio', duration_s=60):
        tests.core.reset(self.dtab)

        dev0 = self.dtab.values()[0]
        groups = tests.core.wait_for_groups(self.dtab, {'1': self.dtab.keys()})
        self.logc('Got groups: %s' % groups, 1)

        # start audio on all groups
        self.logc('Starting audio on group 1, using head: %s' % dev0.host, 1)
        if app == 'webradio':
            dev0.head_call(
                    'group.1',
                    'app.webradio',
                    'play_station',
                    {'url': 'http://ice.somafm.com/bootliquor',
                     'name': 'SomaFM: Boot Liquor'})
        elif app == 'pandora':
            start_pandora_station(dev0, '1')
        else:
            self.log('Waiting 10s for an app to start')
            time.sleep(10)

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


        master_id = groups['1']['master']
        self.logc('Have master: %s' % master_id, 1)
        non_master_ids = set(self.dtab) - set([master_id])

        start_time = time.time()
        while time.time() - start_time < duration_s:
            time.sleep(2)

            reset_n = random.randint(1, len(non_master_ids))
            #reset_n = random.randint(1, 1)
            stop_dev_ids = random.sample(list(non_master_ids), reset_n)
            self.logc('Resetting %s devs: %s' % (reset_n, stop_dev_ids), 1)

            reset_devs = {}
            for stop_dev_id in stop_dev_ids:
                reset_devs[stop_dev_id] = self.dtab[stop_dev_id]
            tests.core.reset(reset_devs, no_cluster=True, no_distributor_check=True)

            self.logc('Waiting for audio advancing on all devs', 1)

            advance_start_time = time.time()
            while True:
                if time.time() - advance_start_time > 90:
                    raise AssertionError('Waiting for audio advancing timed out')
                played_times = {}
                for dev_id, dev in self.dtab.iteritems():
                    played_times[dev_id] = dev.get_state(
                            'beep.playnet')['written_track_time']
                print 'GOT PLAYED TIMES: %s' % played_times

                time.sleep(1)
                all_good = True
                for dev_id, dev in self.dtab.iteritems():
                    try:
                        dev.wait_for_state('beep.playnet', 'written_track_time',
                                played_times[dev_id], test=lambda v,o: v > o,
                                timeout=4.1)
                    except AssertionError:
                        all_good = False
                        break
                if all_good:
                    break

