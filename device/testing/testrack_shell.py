#!/usr/bin/env python

from tests import *
import testrack
from beep import interface

if __name__ == '__main__':
    dtab = testrack.dtab

    print 'Connecting to EvilAp...'
    interface.load_evilap()
    print 'Connected'

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
