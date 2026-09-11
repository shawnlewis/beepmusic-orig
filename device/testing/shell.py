#!/usr/bin/env python

if __name__ == '__main__':
    from beep.interface import DeviceTable, VirtualDevice
    import devs

    dtab = devs.generate_devices()
    from tests import *

    all_tests = [test(dtab) for test in core.BeepTest.__subclasses__()]

    print 'All virtual test devices are in dtab and all tests (including core) preloaded'

    try:
        __IPYTHON__
    except NameError:
        import code
        code.interact(local=locals(),banner='')
    else:
        from IPython.terminal.embed import InteractiveShellEmbed
        InteractiveShellEmbed(banner1='')()
