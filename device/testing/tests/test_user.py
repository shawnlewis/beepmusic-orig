import random, time, beep.interface, tests.core, string
from mc import *

def rand_group_id(N):
    return ''.join(random.choice(string.ascii_uppercase + string.digits) \
            for _ in range(N))


class UserTest(tests.core.BeepTest):
    name = 'User Test'

    def do_idle(self):
        time.sleep(0.9)

    def do_volume_set(self):
        volume = random.randint(0,1000)
        head_dev = random.choice(self.dtab.values())
        self.logc('Set volume => %d' % volume)
        head_dev.head_call(
                self.context, 'audio', 'set_master_volume',
                {'volume':random.randint(0,1000)})

    def do_skip(self):
        head_dev = random.choice(self.dtab.values())
        self.logc('Skip to next track')
        head_dev.head_call(
                self.context, 'audio', 'skip', {})

    def do_pause(self):
        head_dev = random.choice(self.dtab.values())
        self.logc('Pause')
        head_dev.head_call(
                self.context, 'audio', 'pause', {})

    def do_resume(self):
        head_dev = random.choice(self.dtab.values())
        self.logc('Resume')
        head_dev.head_call(
                self.context, 'audio', 'smart_resume', {})

    def do_select_group(self):
        groups = tests.core.get_groups(self.dtab)
        self.context = 'group.' + random.choice(groups.keys())
        self.logc('Select context => %s' % self.context)

    def do_create_group(self):
        n_devices = random.randint(1, len(self.dtab))
        group_id = rand_group_id(20)
        head_dev = random.choice(self.dtab.values())
        devices_to_group = random.sample(self.dtab, n_devices)
        for dev_id in devices_to_group:
            self.logc('Move %s to %s' % (dev_id, group_id))
            head_dev.ubus_call(
                    'beep.manager',
                    'set_group',
                    {'device_id':dev_id,'group_id':group_id})

        time.sleep(4.9)

    def test(self):
        tests.core.stop_all(self.dtab)
        time.sleep(2)
        tests.core.start_all(self.dtab)

        # User model
        IdleState = RunnableState(self.do_idle)
        VolumeSetState = RunnableState(self.do_volume_set)
        SkipState = RunnableState(self.do_skip)
        PauseState = RunnableState(self.do_pause)
        ResumeState = RunnableState(self.do_resume)
        #CastState = RunnableState(self.do_cast)
        SelectGroupState = RunnableState(self.do_select_group)
        CreateGroupState = RunnableState(self.do_create_group)

        IdleState.set_ttable([
                (None, 10),
                (VolumeSetState, 1),
                (SkipState, 1),
                (PauseState, 1),
                (ResumeState, 1),
                #(CastState, 1),
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

        #CastState.set_ttable([
        #        (None, 1),
        #        (IdleState, 4)])

        SelectGroupState.set_ttable([
                (None, 1),
                (IdleState, 9)])

        CreateGroupState.set_ttable([
                (None, 1),
                (SelectGroupState, 8),
                (IdleState, 1)])

        # Run it!
        model = MarkovChain(SelectGroupState)
        for _ in xrange(500):
            model.state.run()
            model.step()
            time.sleep(0.1)
