import unittest
import beep.discovery
from test_helloworld import HelloWorldTest


class TestHelloWorld(unittest.TestCase):
    def setUp(self):
        dtab = beep.discovery.get_real_device_table()
        dtab = {'beep-00dd81':dtab['beep-00dd81']}
        self.beeptest = HelloWorldTest(dtab)

    def test_helloworld(self):
        self.beeptest.run()

if __name__ == '__main__':
    unittest.main()
