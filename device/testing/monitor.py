#!/usr/bin/python
"""Monitors devices defined in params.py

(with style)

"""
import curses, time, threading, sys, getopt
import Queue as _Queue

from beep.interface import *
from beep.discovery import *

import testrack

ONEYEAR = 365 * 24 * 60 * 60

POLL_INTERVAL = 1
POLL_TIMEOUT = 2
NEAR_SEQ_WINDOW = 10
WINDOW_WIDTH = 40
C_WINDOW = 10
C_ERROR = 9

active_devices = []

def query_beephead(host):
    result_json = {}
    result2_json = {}
    result_manager_json = {}
    result_playnet_json = {}

    try:
        result_manager = low_level_head_call(
                    host,
                    'beep.manager',
                    'get_state',
                    {}, timeout=POLL_TIMEOUT)
        result_manager_json = result_manager['result']
    except:
        return (None, None, None, None)
    if not result_manager:
        return (None, None, None, None)

    try:
        result_playnet = low_level_head_call(
                    host,
                    'beep.playnet',
                    'get_state',
                    {}, timeout=POLL_TIMEOUT)
        result_playnet_json = result_playnet['result']
    except:
        return (None, None, None, None)

    try:
        result = low_level_head_call(
                    host,
                    'beep.head',
                    'events',
                    {'seq':0}, timeout=POLL_TIMEOUT)
        result_json = result['result']
    except:
        return (None, None, None, None)
    if not result:
        return (None, None, None, None)

    near_seq = result_json['seq'] - NEAR_SEQ_WINDOW
    if near_seq < 1:
        near_seq = 1
    try:
        result2 = low_level_head_call(
                    host,
                    'beep.head',
                    'events',
                    {'seq':near_seq}, timeout=POLL_TIMEOUT)
        result2_json = result2['result']
    except:
        return (None, None, None, None)
    if not result:
        return (None, None, None, None)

    return (result_json['data'], result2_json['data'], result_manager_json,
            result_playnet_json)

def get_host(device):
    return '%s:%s' % (device['host'], device['http_port'])

class DeviceWindow:
    """Wrapped curses window that displays the status of a specific device"""

    PLAY_PAUSE = {
            'playing' : '|>',
            'paused' : '||',
            'working' : '??',
    }

    def __init__(self, parent_window, device):
        self.name = '%s (%s)' % (device['name'], device['id'])
        self.dev_id = device['id']
        if 'interface' in device.keys():
            self.interface = device['interface']
        else:
            self.interface = None
        self.state = None
        self.events = None
        self.evil_info = None
        self.mgr = None

        self.max_y, self.max_x = parent_window.getmaxyx()
        self.container = curses.newpad(self.max_y - 2, WINDOW_WIDTH)
        self.win = self.container.subpad(self.max_y - 6, WINDOW_WIDTH - 4, 2, 2)

    def print_state_tree(self):
        if not self.state:
            self.win.addstr('<no state>\n', curses.color_pair(C_ERROR))
            return

        for k,v in self.state.items():
            if not k.startswith('group.'):
                continue

            group_label = k[6:]
            if len(group_label) > WINDOW_WIDTH - 20:
                group_label = '...%s' % group_label[-WINDOW_WIDTH + 20:]

            self.win.addstr('* %s ' % group_label)
            if k[6:] in self.mgr['groups']:
                self.win.addstr('(%s) ' % (self.mgr['groups'][k[6:]]))

            for ev in v:
                if ev['object'] != 'audio':
                    continue
                state = ev['state']
                if not isinstance(state, dict):
                    self.win.addstr('\n')
                    break

                self.win.addstr('%s ' % \
                        DeviceWindow.PLAY_PAUSE[state['audio_state']])

                #if 'station' in state:
                #    station = state['station']
                #    self.win.addstr('%s:%s' % \
                #            (station['app'][4:],
                #             station['name'][:10]))
                #    if len(station['name']) > 10:
                #        self.win.addstr('...')

                self.win.addstr('\n')

                players = state['players']
                if not isinstance(players, dict):
                    break

                for player in players.keys():
                    self.win.addstr('  - %s\n' % player)

    def print_recent_events(self):
        if not self.events:
            self.win.addstr('<no events>\n', curses.color_pair(C_ERROR))
            return

        for k,v in self.events.items():
            if not k.startswith('group.'):
                continue

            group_label = k[6:]
            if len(group_label) > 8:
                group_label = '...%s' % group_label[-5:]

            group_label = group_label.rjust(8)

            for ev in v:
                obj_name = ev['object']
                ev_type = ev['event_type']
                self.win.addstr('%s:%s:%s\n' % (group_label, obj_name, ev_type))

    def print_playnet_state(self):
        for k in ['written_track_time', 'streambuf_free', 'output_used', 'gain',
                  'cookie', 'bitrate']:
            self.win.addstr('%s: %s\n' % (k, self.playnet[k]))

    def print_evil_ap(self):
        if not self.interface:
            self.win.addstr('Unknown (virtual?) interface')
            return

        if not self.evil_info:
            self.win.addstr('<EvilAP info unavailable>', curses.color_pair(C_ERROR))
            return

        for k,v in self.evil_info.items():
            if any(v):
                self.win.addstr('%s:%s\n' % (k.rjust(10), v), curses.color_pair(C_ERROR))
            else:
                self.win.addstr('%s:%s\n' % (k.rjust(10), v))

    def print_separator(self):
        self.win.addstr('\n')
        self.win.addstr('-' * (WINDOW_WIDTH-4), )
        self.win.addstr('\n')

    def safe_print(self, fn):
        try:
            fn()
        except:
            #import traceback
            #open('monitor_error.txt', 'a').write(traceback.format_exc())
            #orig_y, orig_x = curses.getsyx()
            #open('monitor_error.txt', 'a').write('%s %s\n' % (orig_y, orig_x))
            #self.win.move(orig_y, orig_x)
            #self.win.move(self.max_y-7, 0)
            self.win.addstr('(Truncated)', curses.color_pair(C_ERROR))

    def render(self):
        self.container.bkgd(' ', curses.color_pair(C_WINDOW)|curses.A_BOLD)
        self.container.border()
        self.container.addstr(0, 2, ' %s ' % self.name)
        self.win.erase()
        self.win.move(0,0)

        self.safe_print(self.print_state_tree)
        self.print_separator()
        self.safe_print(self.print_recent_events)
        self.print_separator()
        self.safe_print(self.print_playnet_state)
        self.print_separator()
        self.safe_print(self.print_evil_ap)

    def update(self, state, events, mgr, playnet, evil_ap=None):
        self.state = state
        self.events = events
        self.mgr = mgr
        self.playnet = playnet
        if evil_ap and self.interface:
            try:
                self.evil_info = evil_ap.get_evil_info(self.interface)
            except ValueError:
                self.evil_info = None
            except:
                sys.stderr.write('Error during update for interface %s' % self.interface)

class Monitor:
    """Main monitor class handles entry and main loop"""

    def __init__(self, evil_ap=None):
        self.evil_ap = evil_ap
        self.updates = _Queue.Queue(maxsize=16)
        self.network_threads = []
        for device in active_devices:
            self.network_threads.append(
                    threading.Thread(
                        target = self.do_networking,
                        args = (device,)))
        self.num_devices = len(active_devices)
        self.dev_offset = 0

        self.main_win = curses.initscr()
        self.main_win.nodelay(True)
        self.max_y, self.max_x = self.main_win.getmaxyx()
        self.max_onscreen_dev = int((self.max_x - 4) / WINDOW_WIDTH)
        self.head_win = self.main_win.subwin(1, self.max_x, 0, 0)
        self.status_win = self.main_win.subwin(1, self.max_x, self.max_y-1, 0)

        curses.curs_set(0)
        curses.noecho()
        curses.start_color()
        curses.init_pair(C_WINDOW, curses.COLOR_WHITE, curses.COLOR_BLACK)
        curses.init_pair(C_ERROR, curses.COLOR_BLACK, curses.COLOR_RED)

        self.device_windows = {}
        for device in active_devices:
            self.device_windows[device['id']] = \
                    DeviceWindow(self.main_win, device)

    def do_networking(self, device):
        while True:
            state, events, mgr, playnet = query_beephead(get_host(device))
            self.updates.put(
                    (device['id'], state, events, mgr, playnet), ONEYEAR)

            if self.shutdown_event.wait(POLL_INTERVAL):
                return

    def write_head(self, msg, attr=0):
        self.head_win.erase()
        self.head_win.addstr(0, 0, msg, attr)
        self.head_win.noutrefresh()

    def update_position(self):
        foot_str = 'Showing %d-%d of %d devices.' % \
                (self.dev_offset + 1,
                 min(self.dev_offset + self.max_onscreen_dev, self.num_devices),
                 self.num_devices)
        self.write_status(foot_str)
        head_str = 'Current Position: %s[%s]%s' % \
                ('#' * self.dev_offset,
                 '#' * self.max_onscreen_dev,
                 '#' * (self.num_devices - self.dev_offset - self.max_onscreen_dev))
        self.write_head(head_str)

    def write_status(self, msg, attr=0):
        self.status_win.erase()
        self.status_win.addstr(0, 0, msg, attr)
        self.status_win.noutrefresh()

    def main(self):
        # Start network thread
        self.shutdown_event = threading.Event()
        for t in self.network_threads:
            t.start()

        self.update_position()
        quit = False
        while not quit:
            # Process updates
            while not self.updates.empty():
                update = self.updates.get()
                self.device_windows[update[0]] \
                        .update(update[1],update[2],update[3],update[4],self.evil_ap)
                self.updates.task_done()

            # Render windows
            for device_id, device_window in self.device_windows.items():
                device_window.render()

            # Place windows
            n = 0
            o = 0
            for device_id, device_window in sorted(self.device_windows.items()):
                o += 1

                if o <= self.dev_offset:
                    continue

                if n >= self.max_onscreen_dev:
                    break

                device_window.container.noutrefresh(
                        0, 0, # pad origin
                        1, 2 + n * WINDOW_WIDTH, # screen origin
                        self.max_y - 1, self.max_x - 2)
                n += 1

            # Wait for input
            key = self.main_win.getch()
            if key == 113: # q
                self.shutdown_event.set()
                with self.updates.mutex:
                    self.updates.queue.clear()
                    self.updates.all_tasks_done.notify_all()
                    self.updates.unfinished_tasks = 0
                quit = True # Not necessary, just for clarity.
                break

            elif key == 104: # h (left)
                if self.dev_offset > 0:
                    self.dev_offset -= 1
                    self.update_position()
                else:
                    self.write_status('Already at left boundary', curses.color_pair(C_ERROR))
            elif key == 108: # j (right)
                if self.dev_offset < self.num_devices - self.max_onscreen_dev:
                    self.dev_offset += 1
                    self.update_position()
                else:
                    self.write_status('Already at right boundary', curses.color_pair(C_ERROR))
            elif key != -1:
                    self.write_status('Unknown keycode: %d (Press <h> and <j> to move left or right; <q> to quit)' % key)

            # Actually perform draw
            curses.doupdate()

            time.sleep(0.1)

def _find_device(dev_id,silent=False):
    try:
        matched_id = dev(dev_id)
        for device in all_devices:
            if device['id'] == matched_id:
                return device

        if not silent:
            print 'Device %s not found.' % dev_id
            sys.exit(-1)
        else:
            return None
    except NotFoundError:
        return None


def show_help():
    print 'monitor.py'
    print ''
    print 'Provides a nice interface for monitoring beephead state'
    print 'across multiple devices.'
    print ''
    print 'Usage: python monitor.py [-h|--help] [--no-evilap] [dev_ids ...]'
    print ''
    print '    -h,--help        Shows this information and exits'
    print '    -q,--quiet       Silently fail when dev_id is not found in params.py'
    print '    --testrack       Monitor testrack devices (no discovery)'
    print '    dev_ids          A list of device_id\'s to monitor. Default: all'

if __name__ == '__main__':
    optspec = 'hq'
    optlongspec = [
        'help',
        'quiet',
        'no-evilap',
        'testrack'
    ]

    try:
        optlist, args = getopt.getopt(sys.argv[1:], optspec, optlongspec)
    except getopt.GetoptError as e:
        sys.stderr.write('Syntax error: %s\n\n' % str(e))
        show_help()
        sys.exit(0)

    evil_ap = None

    if len(optlist) > 0:
        opt_keys = zip(*optlist)[0]
    else:
        opt_keys = tuple()

    if ('-h' in opt_keys) or ('--help' in opt_keys):
        show_help()
        sys.exit(0)

    silent = ('-q' in opt_keys) or ('--quiet' in opt_keys)

    if '--testrack' not in opt_keys:
        print 'Searching for beeps...',
        sys.stdout.flush()
        discovered_devices = get_devices(3)
        print 'Found %d!' % len(discovered_devices)
        time.sleep(0.5)
    else:
        discovered_devices = {}
        for d in testrack.devs:
            key = '%s.local:80' % d['id']
            discovered_devices[key] = {
                    'local_device': {
                        'name': d['id'],
                        'device_id': d['id']}}

    # if user specified some on the command line make sure we found them, and
    # then limit the set we display to those specified.
    if args:
        found_devs = set(discovered_devices)
        print 'Checking args: %s against found_devs: %s' % (args, found_devs)
        specified_devs = set(args)
        leftover = specified_devs.difference(found_devs)
        if leftover:
            print 'Specified devices that were not found on the network: %s' % leftover
            sys.exit(1)
        new_discovered_devices = {}
        for d in specified_devs:
            new_discovered_devices[d] = discovered_devices[d]
        discovered_devices = new_discovered_devices

    found_evil = False
    for k,v in discovered_devices.iteritems():
        host, port = k.split(':')
        evil_interface = None

        # if the device is in the testrack, assign the correct evil interface
        dev_id = host.split('.')[0]
        if dev_id in testrack.dtab:
            evil_interface = testrack.dtab[dev_id].evil_interface
            print 'found evil interface for testdev: %s %s' % (
                    dev_id, evil_interface)
            found_evil = True

        active_devices.append({
            'name' : v['local_device']['name'],
            'id' : v['local_device']['device_id'],
            'host' : host,
            'http_port' : port,
            'evilap' : {},
            'interface' : evil_interface
        })
    if found_evil:
        print 'Connecting to evilap...'
        evil_ap = load_evilap()  # Defined in beep_interface
        if not evil_ap:
            print 'Failure'
        else:
            print 'Success'

    if len(active_devices) == 0:
        sys.stderr.write('No valid devices\n')
        sys.exit(-1)

    M = Monitor(evil_ap=evil_ap)
    try:
        M.main()
    finally:
        M.shutdown_event.set()
        curses.endwin()
        print 'Shutting down network thread...'
        sys.stdout.flush()
        for t in M.network_threads:
            t.join()
