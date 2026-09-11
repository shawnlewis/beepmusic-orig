import random, time, string

from threading import Thread, Event
from Queue import Queue

import beep.interface, tests.core
from mc import *

def get_evil_devs(dtab):
    """Return dicts of evil and good devices given a dtab"""
    evil = {}
    good = {}
    for dev_id, dev in dtab.iteritems():
        if dev.evil_interface:
            evil[dev_id] = dev
        else:
            good[dev_id] = dev
    return evil, good

def generate_user_name():
    return random.choice(
            ['James','John','Robert','Michael','William','David','Richard',
             'Charles','Joseph','Thomas','Christopher','Daniel','Paul',
             'Mark','Donald','George','Kenneth','Steven','Edward','Brian',
             'Ronald','Anthony','Kevin','Jason','Matthew','Gary','Timothy',
             'Jose','Larry','Jeffrey','Frank','Scott','Eric','Stephen',
             'Andrew','Raymond','Gregory','Joshua','Jerry','Dennis','Walter',
             'Patrick','Peter','Harold','Douglas','Henry','Carl','Arthur',
             'Ryan','Roger']) + '%03d' % random.randint(0,999)

def rand_group_id(N):
    """Generate a random group ID string of length N"""
    return ''.join(random.choice(string.ascii_lowercase + string.digits) \
            for _ in range(N))

def rand_interval(minval,maxval):
    return minval + (random.random() * (maxval - minval))

class User(object):
    def do_idle(self):
        self.refractory_period = rand_interval(5,10)
        #self.logc('(%s) Plans his next move carefully...' % self.label)

    def do_volume_set(self):
        self.refractory_period = rand_interval(0.25,0.75)

        volume = random.randint(0,1000)
        head_dev = random.choice(self.dtab.values())
        self.logc('(%s) Sets master volume => %d for %s'
                % (self.label, volume, self.context))
        head_dev.head_call(
                self.context, 'audio', 'set_master_volume',
                {'volume':random.randint(0,1000)})

    def do_skip(self):
        self.refractory_period = rand_interval(1.5,4.5)
        head_dev = random.choice(self.dtab.values())
        self.logc('(%s) Skips to next track for %s' % (self.label, self.context))
        head_dev.head_call(
                self.context, 'audio', 'skip', {})

    def do_pause(self):
        self.refractory_period = rand_interval(1,2)
        head_dev = random.choice(self.dtab.values())
        self.logc('(%s) Pauses audio on %s' % (self.label, self.context))
        head_dev.head_call(
                self.context, 'audio', 'pause', {})

    def do_resume(self):
        self.refractory_period = rand_interval(1,2)
        head_dev = random.choice(self.dtab.values())
        self.logc('(%s) Resumes audio on %s' % (self.label, self.context))
        head_dev.head_call(
                self.context, 'audio', 'smart_resume', {})

    def do_cast(self):
        self.refractory_period = rand_interval(1.5,4.5)
        self.logc('(%s) Casts to %s !!!NOT IMPLEMENTED!!!' % (self.label, self.context))

    def do_select_group(self):
        self.logc('(%s) Selecting context' % self.label)
        self.refractory_period = rand_interval(1,5)
        groups = tests.core.get_groups(self.dtab)
        self.context = 'group.' + random.choice(groups.keys())
        self.logc('(%s) Selects context => %s' % (self.label, self.context))

    def do_create_group(self):
        self.logc('(%s) Creating group' % self.label)
        self.refractory_period = rand_interval(5,10)
        n_devices = random.randint(1, len(self.dtab))
        group_id = rand_group_id(8)
        head_dev = random.choice(self.dtab.values())
        devices_to_group = random.sample(self.dtab, n_devices)
        for dev_id in devices_to_group:
            self.logc('(%s) moves %s to %s' % (self.label, dev_id, group_id))
            head_dev.head_call(
                    'system',
                    'manager',
                    'set_group',
                    {'device_id':dev_id,'group_id':group_id})

    def __init__(self, label, logc, dtab):
        self.label = label
        self.logc = logc
        self.dtab = dtab
        self.refractory_period = 0

    def run(self, test_period, shutdown):
        IdleState = RunnableState(self.do_idle)
        VolumeSetState = RunnableState(self.do_volume_set)
        SkipState = RunnableState(self.do_skip)
        PauseState = RunnableState(self.do_pause)
        ResumeState = RunnableState(self.do_resume)
        CastState = RunnableState(self.do_cast)
        SelectGroupState = RunnableState(self.do_select_group)
        CreateGroupState = RunnableState(self.do_create_group)

        IdleState.set_ttable([
                (None, 1),
                (VolumeSetState, 1),
                (SkipState, 1),
                (PauseState, 1),
                (ResumeState, 1),
                (CastState, 1),
                (SelectGroupState, 1),
                (CreateGroupState, 1)])

        VolumeSetState.set_ttable([
                (None, 8),
                (IdleState, 1)])

        SkipState.set_ttable([
                (None, 1),
                (IdleState, 4)])

        PauseState.set_ttable([
                (None, 1),
                (ResumeState, 1),
                (IdleState, 4)])

        ResumeState.set_ttable([
                (None, 1),
                (PauseState, 1),
                (IdleState, 4)])

        CastState.set_ttable([
                (None, 1),
                (IdleState, 4)])

        SelectGroupState.set_ttable([
                (None, 1),
                (IdleState, 9)])

        CreateGroupState.set_ttable([
                (None, 1),
                (SelectGroupState, 8),
                (IdleState, 1)])

        model = MarkovChain(SelectGroupState)
        start_time = time.time()
        self.logc('(%s) Enters the fray!' % self.label)
        while time.time() - start_time < test_period and not shutdown.is_set():
            model.state.run()
            model.step()
            #self.logc('(%s) Next turn in %0.2f seconds' % (self.label, self.refractory_period))
            time.sleep(self.refractory_period)
        self.logc('(%s) Retires...' % self.label)

class Gremlin(object):
    def _random_target(self):
        """Return random device_id from dtab"""
        return random.sample(self.eviltab,1)[0]

    def _kill_network(self, dev_id):
        """Set fatal evil AP parameters on a device"""
        pass

    def _maim_network(self, dev_id):
        """Set non-fatal evil AP parameters on a device"""
        pass

    def _kill_services(self, dev_id):
        """SIGKILL services on a device"""
        pass

    def _grant_mercy(self, dev_id):
        """Reset any evil AP effects on a device"""
        pass

    def do_idle(self):
        self.refractory_period = rand_interval(5,10)
        #self.logc('(Gremlin) Bides its time')

    def do_kill_network(self):
        self.refractory_period = rand_interval(20,40)
        victim = self._random_target()
        self._kill_network(*_random_target)
        self.logc('(Gremlin) Kills %s network connectivity!' % victim)

    def do_maim_network(self):
        self.refractory_period = rand_interval(20,40)
        victim = self._random_target()
        self._maim_network(*_random_target)
        self.logc('(Gremlin) Injures %s network connectivity!' % victim)
        # Show parameter info below

    def do_kill_services(self):
        self.refractory_period = rand_interval(20,40)
        victim = self._random_target()
        self._kill_services(*_random_target)
        self.logc('(Gremlin) Destroys %s vital services!' % victim)

    def do_grant_mercy(self):
        self.refractory_period = rand_interval(3,6)
        victim = self._random_target()
        self._grant_mercy(*_random_target)
        self.logc('(Gremlin) Withdraws attack on %s network connectivity!' % victim)

    def __init__(self, logc, eviltab):
        self.logc = logc
        self.eviltab = eviltab
        self.refractory_period = 0

    def run(self, test_period, shutdown):
        IdleState = RunnableState(self.do_idle)
        KillNetworkState = RunnableState(self.do_kill_network)
        MaimNetworkState = RunnableState(self.do_maim_network)
        KillServicesState = RunnableState(self.do_kill_services)
        GrantMercyState = RunnableState(self.do_grant_mercy)

        IdleState.set_ttable([
                (None, 1),
                (KillNetworkState, 1),
                (MaimNetworkState, 1),
                (KillServicesState, 1),
                (GrantMercyState, 1)])

        KillNetworkState.set_ttable([
                (None, 1),
                (IdleState, 1),
                (MaimNetworkState, 1),
                (KillServicesState, 1),
                (GrantMercyState, 1)])

        MaimNetworkState.set_ttable([
                (None, 1),
                (IdleState, 1),
                (KillNetworkState, 1),
                (KillServicesState, 1),
                (GrantMercyState, 1)])

        KillServicesState.set_ttable([
                (None, 1),
                (IdleState, 1),
                (KillNetworkState, 1),
                (MaimNetworkState, 1),
                (GrantMercyState, 1)])

        GrantMercyState.set_ttable([
                (None, 1),
                (IdleState, 1),
                (KillNetworkState, 1),
                (MaimNetworkState, 1),
                (KillServicesState, 1)])

        model = MarkovChain(IdleState)
        start_time = time.time()
        self.logc('(Gremlin) A wild Gremlin appears!')
        while time.time() - start_time < test_period and not shutdown.is_set():
            model.state.run()
            model.step()
            #self.logc('(Gremlin) Next turn in %0.2f seconds' % self.refractory_period)
            time.sleep(self.refractory_period)
        self.logc('(Gremlin) Flees from combat!')

class AbusiveTest(tests.core.BeepTest):
    name = 'Abusive Test'

    def test(self,n_users=10,test_period=300,n_periods=3):
        shutdown = Event()
        evil, good = get_evil_devs(self.dtab)

        for x in xrange(n_periods):
            # Reset devices and users
            tests.core.reset(self.dtab)
            shutdown.clear()

            # Spawn User processes
            users = []
            user_threads = []
            for x in xrange(n_users):
                username = generate_user_name()
                self.logc('(Dungeon Master) Spawn %s' % username)

                user = User(username, self.logc, self.dtab)
                users.append(user)
                user_thread = Thread(target=user.run,args=(test_period,shutdown))
                user_thread.daemon = True
                user_threads.append(user_thread)
                user_thread.start()

            # Spawn one Gremlin process
            gremlin_thread = None
            if len(evil) > 0:
                self.logc('(Dungeon Master) Detected vulnerable devices! Spawning a Gremlin...')
                gremlin = Gremlin(self.logc,evil)
                gremlin_thread = Thread(target=gremlin.run,args=(test_period,shutdown))
                gremlin_thread.daemon = True
                gremlin_thread.start()
            else:
                self.logc('(Dungeon Master) No vulnerable devices available.  The land prospers!')

            # Stop Users (or wait for them to end?)
            self.logc('(Dungeon Master) Awaiting the end of combat...')
            try:
                while True:
                    users_done = [False] * n_users
                    for i in xrange(len(user_threads)):
                        user_threads[i].join(1)
                        users_done[i] = not user_threads[i].isAlive()

                    gremlin_done = True
                    if gremlin_thread:
                        gremlin_done = gremlin_thread.join(1)

                    if all(users_done) and gremlin_done:
                        break

            except KeyboardInterrupt:
                self.logc('(Dungeon Master) Closing up the rulebooks')
                shutdown.set()
                for user_thread in user_threads:
                    user_thread.join()
                if gremlin_thread:
                    gremlin_thread.join()

            self.logc('(Dungeon Master) Session closed')
            # Stop Gremlin, restore evil AP

            # Wait to allow for convergence

            # Validate state
            self.loga('(Dungeon Master) Validating dungeon state')
            tests.core.check_minimal(self.dtab)

            self.log('(Dungeon Master) Test complete')

