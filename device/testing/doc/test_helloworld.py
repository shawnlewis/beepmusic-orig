import beep.testcore, random, time

class HelloWorldTest(beep.testcore.BeepTest):
    name = 'Hello World Test'
    requires_real_devices = False

    def test(self):
        dev_id = random.sample(self.dtab, 1)[0]
        dev = self.dtab[dev_id]
        self.log('Testing %s' % dev_id)

        self.logc('Setting group to "hello_world"')
        dev.ubus_call(
                'beep.manager',
                'set_group',
                {'device_id':dev_id, 'group_id':'hello_world'})
        self.loga('Verifying group is "hello_world"')
        dev.wait_for_state(
                'beep.manager',
                'local_device sink_id',
                'hello_world',
                timeout = 30)

if __name__ == '__main__':
    import beep.interface
    dev = beep.interface.VirtualDevice('localhost:32001','../dev.A')
    try:
        dev.stop()
    except:
        pass
    dev.start()
    time.sleep(5)
    test = HelloWorldTest(dev.dtab())
    test.run()
    dev.stop()

