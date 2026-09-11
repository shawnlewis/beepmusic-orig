import tests.core

class CycleServicesTest(tests.core.BeepTest):
    name = 'Cycle Services'

    def test(self, rep=2):
        for i in xrange(rep):
            tests.core.reset(self.dtab)
            tests.core.get_groups(self.dtab)
