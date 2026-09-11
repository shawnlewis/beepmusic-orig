#!/usr/bin/python
import random, time, sys, requests
from beep_interface import low_level_call, head_call
from params import all_devices, groups
from params import TESTER_INTERVAL as INTERVAL_RANGE

def log(string):
    global host
    print('(Tester @ %s) %s' % (host, str(string)))

def random_set_group(host):
    group = random.choice(groups)

    device_set = random.sample(all_devices, random.randint(1,len(all_devices)))
    for device in device_set:
        params = {
            'device_id' : device['id'],
            'group_id' : group
        }

        log('  set_group on %s to %s' % (device['id'], group))
        head_call(host, 'system', 'manager', 'set_group', params)

def random_set_group_indeterminate(host):
    device = random.choice(all_devices)
    params = {
        'device_id' : device['id']
    }

    log('  Indeterminate set_group on %s' % device['id'])
    head_call(host, 'system', 'manager', 'set_group', params)

pandora_stations = (
    ('Avett Brothers Radio', '1284607538395009'),
    ('The Shins Radio', '2112801787115393'),
    ('Jazz Radio', '2014034719176577'),
)

pandora_device_id = 'cbfdb418-a43b-4faa-9246-6bda0e019b3e'

def random_play_pandora_station(host):
    group = random.choice(groups)
    station = random.choice(pandora_stations)

    params = {
            'type' : 'play_station',
            'message' : {
                'deviceId' : pandora_device_id,
                'id' : station[1],
            }
    }
    log('  Play %s on group %s via device %s' % (station[0], group, host))
    ctx = 'group.%s' % group
    head_call(host, ctx, 'app.pandora', 'message', params)

commands = (
    random_set_group,
    #random_set_group_indeterminate,
    random_play_pandora_station,
)

def do_random_command(host):
    cmd = random.choice(commands)
    cmd(host)

if __name__ == '__main__':
    global host
    host = sys.argv[1]

    while True:
        try:
            do_random_command(host)
        except requests.exceptions.ConnectionError:
            break
        except:
            print "Unexpected error: ", sys.exc_info()[0], sys.exc_info()[1]
            break

        wait_time = random.randint(*INTERVAL_RANGE)
        time.sleep(wait_time)
