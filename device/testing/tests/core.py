import datetime, time, sys, unittest, string, random, pprint

from beep import interface
from beep import utils
from beep import virtual_device

# TODO: Use unittest framework so we get this for free?
def is_equal(o1, o2):
    if type(o1) != type(o2):
        return False
    elif isinstance(o1, (int, float, str, unicode)):
        return o1 == o2
    elif isinstance(o1, dict):
        all_keys = set(o1).union(set(o2))
        if len(all_keys) != len(o1) or len(all_keys) != len(o2):
            return False
        for k in all_keys:
            return is_equal(o1[k], o2[k])
    elif isinstance(o1, list):
        for v1, v2 in zip(o1, o2):
            return is_equal(v1, v2)
    elif isinstance(o1, set):
        for v in o1:
            if v not in o2:
                return False
        return True
    else:
        print 'unknown type for is_equal: %s' % type(o1)
        return False

def _log(msg,level=0):
    log_line = '%s %s%s\n' % \
            (datetime.datetime.now().strftime('%Y%m%d %H:%M:%S.%f'),
             '  ' * level,
             msg)
    sys.stderr.write(log_line)

def reset(dtab, no_cluster=False, no_distributor_check=False):
    have_evil = False
    for dev in dtab.itervalues():
        if dev.evil_interface:
            have_evil = True
    if have_evil:
        interface.load_evilap()
        interface.clear_evilap()

    if not no_cluster:
        set_cluster_all(dtab)
    tries = 0
    while True:
        tries += 1
        try:
            stop_all(dtab)
            set_group_all(dtab, '1')
            start_all(dtab)
            check_base(dtab, no_distributor_check=no_distributor_check)
            break
        except (AssertionError, virtual_device.VirtualDeviceError):
            _log('Stopping and starting devices failed, Restarting. tries: %s' %
                    tries)
            if tries >= 3:
                raise

    # TODO: shawn there is some other state we need to wait for, I think
    # beephead may not be ready. Figure out how to wait til EVERYTHING
    # is ready.
    # For now we sleep
    time.sleep(2)

def set_cluster_all(dtab):
    chars = string.ascii_lowercase + string.digits
    unique_id = ''.join(random.choice(chars) for _ in xrange(5))

    for dev_id, dev in dtab.iteritems():
        dev.uci_set('data', 'cluster_id', 'test.%s' % unique_id)
        dev.uci_set('devel', 'force_cluster', '1')
        dev.uci_commit()

def set_group_all(dtab, group_id):
    for dev_id, dev in dtab.iteritems():
        dev.uci_set('data', 'group_id', group_id)
        dev.uci_commit()

def stop_all(dtab):
    def log_and_stop(dev_id, dev):
        _log('Stop %s' % dev_id)
        dev.stop()
    utils.parallel_do(
            [utils.make_closure(log_and_stop, dev_id, dev)
                for dev_id, dev
                in dtab.iteritems()])

def start_all(dtab):
    # really does not like to be parallelized. I tried changing how we
    # start beep services and couldn't get it to work
    for dev_id, dev in dtab.iteritems():
        _log('Start %s' % dev_id)
        dev.start()

def check_minimal(dtab):
    utils.parallel_do(
            [utils.make_closure(device_check_minimal, dev_id, dev)
                for dev_id, dev in dtab.iteritems()])

def device_check_minimal(dev_id, dev):
    critical_components = (
            'beep.cloud',
            'beep.comm.control',
            'beep.distributor',
            'beep.head',
            'beep.health',
            'beep.manager',
            'beep.playnet',
            'urelay')

    now = time.time()
    missing_components = critical_components

    #try:
    while True:
        missing_components = tuple([comp for comp in critical_components if
                comp not in dev.ubus_list()])

        if len(missing_components) == 0:
            break;
        if time.time() - now > 60:
            raise AssertionError('timeout: missing %s' % ', '.join(missing_components))

        time.sleep(1.0)

    _log('%s services OK' % dev_id)

    assert dev.wait_for_state('beep.manager', 'local_device sink_id',None, \
            test=lambda v,o: v, timeout=60), 'invalid state: beep.manager: %s ' % dev.get_state('beep.manager')
    assert dev.wait_for_state(
            'beep.manager', 'groups', None,
            test= lambda v, o: len(v) > 0, timeout=60), \
            'never found beep.manager groups table: %s ' % dev.get_state(
                    'beep.manager')
    _log('%s minimal state checks OK' % dev_id)

def check_base(dtab, no_distributor_check=False):
    utils.parallel_do(
            [utils.make_closure(device_check_base, dev_id, dev, no_distributor_check)
                for dev_id, dev
                in dtab.iteritems()])

def device_check_base(dev_id, dev, no_distributor_check):
    critical_components = (
            'beep.cloud',
            'beep.comm.control',
            'beep.distributor',
            'beep.head',
            'beep.health',
            'beep.manager',
            'beep.playnet',
            'urelay')

    now = time.time()
    missing_components =critical_components

    #try:
    while True:
        missing_components = tuple([comp for comp in critical_components if
                comp not in dev.ubus_list()])

        if len(missing_components) == 0:
            break;
        if time.time() - now > 60:
            raise AssertionError('timeout: missing %s' % ', '.join(missing_components))

        time.sleep(1.0)

    _log('%s services OK' % dev_id)

    group_id = dev.uci_get('data', 'group_id')
    assert dev.wait_for_state('beep.manager','local_device sink_id',group_id, \
            timeout=60), 'invalid state: beep.manager: %s ' % dev.get_state('beep.manager')
    assert dev.wait_for_state(
            'beep.manager', 'groups', None,
            test= lambda v, o: len(v) > 0, timeout=60), \
            'never found beep.manager groups table: %s ' % dev.get_state(
                    'beep.manager')
    if not no_distributor_check:
        assert dev.wait_for_state('distributor','audio_state','paused', \
                timeout=60), 'invalid state: distributor'
        assert dev.wait_for_state('playnet','can_st_begin',True, \
                timeout=60), 'invalid state: playnet'
    _log('%s core state checks OK' % dev_id)
    #except RuntimeError:
    #    raise AssertionError('system calls failed')

# returns {<group_id_0>: {'master': <master_dev_id>,
#                         'devices': [<dev_id_0>, <dev_id1>, ...]},
#          ...}
def _get_groups_single_device(dev):
    groups = {}
    manager_state = dev.get_state('beep.manager')
    if not manager_state: # This device is probably not running
        return {}
    if (isinstance(manager_state['devices'], list)
            or isinstance(manager_state['groups'], list)):
        # This happens when a device is starting up
        return {}
    for dev_id, state in manager_state['devices'].iteritems():
        sink_id = state['sink_id']
        if sink_id not in groups:
            groups[sink_id] = {'devices': set()}
        groups[sink_id]['devices'].add(dev_id)
    for group_id, master_dev_id in manager_state['groups'].iteritems():
        # a device state should never trigger this assert
        assert(group_id in groups), 'Group ID %s was not in groups' % group_id

        groups[group_id]['master'] = master_dev_id

    return groups

# Returns device grouping, same format as _get_groups_single_device
# But waits until all devices agree on grouping before returning.
def get_groups(dtab):
    devices_agree = False
    iters_remaining = 20

    while not devices_agree and iters_remaining:
        devices_agree = True

        dev_groups = [_get_groups_single_device(dev) for dev in dtab.itervalues()]

        # check each device agrees on what the groups are
        for dg in dev_groups[1:]:
            if not is_equal(dev_groups[0], dg):
                devices_agree = False

        # check that all devices in dtab are in the groups we know about
        dev_ids = set()
        for group_state in dev_groups[0].itervalues():
            dev_ids.update(group_state['devices'])
        if not is_equal(set(dtab), dev_ids):
            devices_agree = False

        if not devices_agree:
            time.sleep(.5)

        iters_remaining -= 1
    assert devices_agree, 'Devices never agreed on group config'

    return dev_groups[0]

def wait_for_groups(dtab, expected_groups):
    start_time = time.time()
    while time.time() - start_time < 30:
        groups = get_groups(dtab)
        matched = True
        if set(groups.keys()) != set(expected_groups.keys()):
            continue
        for k, devs in expected_groups.iteritems():
            if set(devs) != set(groups[k]['devices']):
                matched = False
                break
        if matched:
            return groups
    raise AssertionError('Never got expected groups. expected: %s actual: %s'
            % (expected_groups, groups))

def as_test_case(test):
    return unittest.FunctionTestCase(test.run)

class BeepTest(object):
    """Base test class; provides some test support methods"""
    name = None
    requires_real_devices = False

    def __init__(self, dtab):
        assert self.name, 'No name specified (required attribute)'
        all_real_devices = False
        if self.requires_real_devices:
            for dev in dtab.values():
                if not dev.is_real_device():
                    raise AssertionError('This test requires real devices')
        self.dtab = dtab

    def test(self, dtab):
        pass

    def run(self, **kwargs):
        """Run the test"""
        param_string = str(kwargs) if len(kwargs) > 0 else '(Defaults)'
        _log('### START %s %s ###' % (self.name, param_string))
        self.test(**kwargs)
        _log('### END %s %s ###' % (self.name, param_string))

    def log(self, msg, level=0):
        """Unformatted log message"""
        _log('%s' % msg, level + 1)

    def logc(self, msg, level=0):
        """Output a command"""
        _log('> %s' % msg, level + 1)

    def loga(self, msg, level=0):
        """Output an assertion/verification"""
        _log('? %s' % msg, level + 1)
