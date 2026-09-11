import random, time, tests.core, string

def rand_group_id(N):
    return ''.join(random.choice(string.ascii_uppercase + string.digits) \
            for _ in range(N))

class GroupingTest(tests.core.BeepTest):
    name = 'Grouping Test'

    def test(self,rep=30):
        tests.core.reset(self.dtab)

        for i in range(rep):
            n_devices = random.randint(1,len(self.dtab))

            group_id = rand_group_id(20)

            # Random device to send set_group to
            head_dev_id = random.sample(self.dtab, 1)[0]
            head_dev = self.dtab[head_dev_id]

            devices_to_group = random.sample(self.dtab, n_devices)

            #self.logc('Moving %d devices to group %s' % (n_devices, group_id))
            for dev in devices_to_group:
                self.logc('Move %s => %s' % (dev, group_id))
                head_dev.ubus_call(
                        'beep.manager',
                        'set_group',
                        {'device_id':dev,'group_id':group_id})

            self.log('Waiting %ds' % (n_devices * 2,))
            time.sleep(n_devices * 2)
            self.loga('Verify group')
            for dev in devices_to_group:
                self.dtab[dev].assert_state(
                        'beep.manager',
                        'local_device sink_id',
                        group_id,'sink_id is not $op, found $val for %s' % dev)

class RapidGroupingTest(tests.core.BeepTest):
    name = 'Rapid Grouping Test'

    def test(self,rep=100,mindelay=0,maxdelay=3.0):
        tests.core.reset(self.dtab)

        for i in range(rep):
            n_devices = random.randint(1,len(self.dtab))

            group_id = rand_group_id(20)

            # Random device to send set_group to
            head_dev_id = random.sample(self.dtab, 1)[0]
            head_dev = self.dtab[head_dev_id]

            devices_to_group = random.sample(self.dtab, n_devices)

            params = {}
            for dev in devices_to_group:
                self.logc('Move %s => %s' % (dev, group_id))
                head_dev.ubus_call(
                        'beep.manager',
                        'set_group',
                        {'device_id':dev,'group_id':group_id})

            if i != rep - 1:
                delay = mindelay + (random.random() * (maxdelay - mindelay))
                self.logc('Waiting %.2fs before next iteration' % delay)
                time.sleep(delay)

        self.logc('Waiting 10s')
        time.sleep(10)

        self.logc('Set group to FINAL')
        for dev_id, dev in self.dtab.iteritems():
            dev.ubus_call(
                    'beep.manager',
                    'set_group',
                    {'device_id':dev_id,'group_id':'FINAL'})

        self.logc('Waiting 10s')
        time.sleep(10)

        self.loga('Verify group')
        for dev_id, dev in self.dtab.iteritems():
            dev.assert_state(
                    'beep.manager',
                    'local_device sink_id',
                    'FINAL','sink_id is not $op, found $val for %s' % dev)


