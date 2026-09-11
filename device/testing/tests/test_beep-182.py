import random, time, beep.interface, tests.core

class Beep182Test(tests.core.BeepTest):
    name = 'BEEP-182 Test (Playnet)'

    def test(self):
        tests.core.stop_all(self.dtab)
        dev_ids = random.sample(self.dtab, 2)
        self.log('Selected %s' % ', '.join(dev_ids))
        source = self.dtab[dev_ids[0]]
        sink = self.dtab[dev_ids[1]]

        self.log('Start source first: %s' % dev_ids[0])
        source.start()

        time.sleep(5)

        self.log('Start sink second: %s' % dev_ids[1])
        sink.start()

        dtab = {
                dev_ids[0]:source,
                dev_ids[1]:sink
        }

        tests.core.check_base(dtab)

        self.log('Setup complete')
        time.sleep(2)

        self.logc('Play webradio')
        source.head_call('group.1','app.webradio','play_station',
                {'name':'SomaFM: Groove Salad',
                 'url':'http://ice.somafm.com/groovesalad',
                 'image_url':'http://somafm.com/logos/512/groovesalad512.png'})

        self.logc('Wait 10s')
        time.sleep(10)

        self.logc('Kill playnet on sink')
        sink.kill_process('playnet')

        self.logc('Wait 10s')
        time.sleep(10)

        self.logc('Restart sink')
        sink.stop()
        time.sleep(2)
        sink.start()

        self.logc('Wait 10s')
        time.sleep(10)

        self.logc('Restart webradio')
        source.head_call('group.1','app.webradio','play_station',
                {'name':'SomaFM: Groove Salad',
                 'url':'http://ice.somafm.com/groovesalad',
                 'image_url':'http://somafm.com/logos/512/groovesalad512.png'})

        self.logc('Wait 10s')
        time.sleep(10)

        source.assert_state('manager','devices',2,test=lambda v,o:len(v) == o,
                failmsg='Source manager devices table does not have two devices')
        source.assert_state('manager','groups',1,test=lambda v,o:len(v) == o,
                failmsg='Source manager groups table does not have only one group')
        source.assert_state('distributor','players',2,test=lambda v,o:len(v) == o,
                failmsg='Source distributor players table does not have two devices')

        sink.assert_state('manager','devices',2,test=lambda v,o:len(v) == o,
                failmsg='Sink manager devices table does not have two devices')
        sink.assert_state('manager','groups',1,test=lambda v,o:len(v) == o,
                failmsg='Sink manager groups table does not have only one group')
        sink.assert_state('distributor','players',0,test=lambda v,o:len(v) == o,
                failmsg='Sink distributor players table is not empty')
