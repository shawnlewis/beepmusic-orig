#!/usr/bin/env python

if __name__ == '__main__':
    import devs
    dtab = devs.generate_devices()

    from tests import *

    all_tests = [test(dtab) for test in core.BeepTest.__subclasses__()]

    for test in all_tests:
        core.stop_all(dtab)
        core.start_all(dtab)
        core.check_base(dtab)
        test.run()
