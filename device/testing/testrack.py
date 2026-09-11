#!/usr/bin/python

from beep.interface import DeviceTable, RealDevice

devs = [
    {'serial': 0, 'id': 'beep-00dcc4', 'evil': None},
    {'serial': 1, 'id': 'beep-00dd5a', 'evil': 'wlan0-1'},
    {'serial': 2, 'id': 'beep-00ddd8', 'evil': 'wlan0-2'},
    {'serial': 3, 'id': 'beep-00dcdf', 'evil': 'wlan0-3'},
    {'serial': 4, 'id': 'beep-00dc97', 'evil': 'wlan0-4'},
    {'serial': 5, 'id': 'beep-00dd24', 'evil': None},
    {'serial': 6, 'id': 'beep-00dd4b', 'evil': None},
    {'serial': 7, 'id': 'beep-00dc9a', 'evil': None},
]

dtab = DeviceTable()
for dev in devs:
    dev_id = dev['id']
    dtab[dev_id] = RealDevice(dev_id + '.local', evil_interface=dev['evil'])
