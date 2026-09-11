# Beep Test API Documentation

Version 1.0

## Overview

The Beep Test API helps simplify the process of performing many kinds of tests
against the Beep software stack.  It is implemented as a set of python modules
and consists of the following components:

- [Discovery](#discovery): Uses bonjour (dns-sd) to find devices on the local
  subnet and provide information on their control interface and http interface.

- [Interface](#interface): An abstraction layer for sending commands, receiving
  feedback and controlling the lifecycle of Beep services.  

- [Test Core](#testcore): Operates on `DeviceTables` and provides an abstract
  `BeepTest` object that encapsulates a sequence of commands and expected
  outputs.


## Quick Start: Hello, World!

Let's create a trivial test of a device's ability to change groups.

First, we write the test.  Put the following in a python file (I'll call this
[`test_helloworld.py`](./test_helloworld.py)):

    import testcore, random

    class HelloWorldTest(testcore.BeepTest):
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

To run the test against a device, make sure one is running and connected
to the local network.  In the python REPL:

    >>> from beep.interface import *
    >>> from test_helloworld import *
    >>> dev = RealDevice('beep-123456.local')
    >>> test = HelloWorldTest(dev.dtab())
    >>> test.run()

You should see the following output:

    20140912 18:38:48.521235 ### START Hello World Test (Defaults) ###
    20140912 18:38:48.522904   Testing beep-123456
    20140912 18:38:48.523718   > Setting group to "hello_world"
    20140912 18:38:49.800902   ? Verifying group is "hello_world"
    20140912 18:38:49.801049 ### END Hello World Test (Defaults) ###

## <a name="discovery"></a> Discovery

Within the Discovery module are a full-featured asynchronous discovery service
object and two simple, synchronous helper functions.

To discover all beeps on the local network:

    >>> from beep.discovery import get_devices
    >>> devices = get_devices()

This blocks for 5 seconds then returns a `dict` of hostnames and the state for
each device.

You may want to discover devices for testing purposes, in which
case it's useful to generate a `DeviceTable` directly from discovery results.
Due to the nature of virtual devices, this functionality is limited to
discovering *only real devices*.

    >>> from beep.discovery import get_real_device_table
    >>> dtab = get_real_device_table()

If you require more fine-grained control of the discovery process, you will need
to use the `Discovery` class directly.  Construct an instance and pass the
callbacks you are interested in (see the `Discovery` docstring for more info).

The `beep.discovery` module also provides a CLI that provides monitoring and
discovery output that can be used by other programs.  To use the CLI:

    $ python -m beep.discovery

For example, to run discovery for 3 seconds and output only state information:

    $ python -m beep.discovery -t 3 -f i

Pass the `-h` flag for more information on the CLI

## <a name="interface"></a> Interface

The Interface module contains a significant part of the communication and
control interfaces available for real and virtual devices alike.  The `Device`
base class is subclassed by `VirtualDevice` and `RealDevice`, which implement
the actual interfaces to both types of devices.  `DeviceTables` are simply
dictionaries of device interfaces (`RealDevices` or `VirtualDevices`) keyed by
`device_id`.

Typically, the interface module is used as a library by instantiating `Device`
and its subclasses.  For the purposes of this section, I assume you have a
variable `device` which is an instance of `RealDevice` (see
[Discovery](#discovery), above, for instructions) and we will be interacting
with the interface module using the REPL.

### beep.interface.Device

The `Device` base class provides a basic but powerful interface.

#### get_state(...)

To get the state for the manager on a device, simply do:

    >>> device.get_state()

To get the state for a different component, pass its name to `get_state`.

    >>> device.get_state('beep.distributor')

`get_state` returns the state of the component that the component string
matches against, so this works too:

    >>> device.get_state('dist')

...but be careful with ambiguous cases, such as `get_state('d')`.  In this case,
`get_state` returns None instead of a potentially confusing state table.

#### assert_state(...)

`Device` provides a convenience method for asserting device states.  To assert
that our device's local volume is 500, we call:

    >>> device.assert_state('beep.distributor','local_volume',500)

`assert_state` does not actually throw an `AssertionError`, but instead returns
a tuple, where the first element is the trueness of the assertion, and the
second element is the actual value.

Suppose we wanted to assert that `local_volume` is *GREATER* than `500`.  By
default, the test function simply checks for equality, but We can pass a test
function to `assert_state`:

    >>> device.assert_state('beep.distributor','local_volume',500,
    ...     lambda o,v: v > o)

Note the order of parameters for our test function: *operand first, then
encountered value*.

If we want to test the value of a state field below the top level, we
can tell `assert_state` to traverse the component's state table by passing a
space-delimited list of fields, e.g.:

    >>> device.assert_state('beep.manager','local_device source_id','-1')

Also note that you aren't limited to primitives.  If the specified field is a
table or array, you can pass an entire table to test against, or pass `None` for
the operand and write a test function that performs more sophisticated
comprehension of the value.

#### wait_for_state(...)

`assert_state` is useful for checking the current state of a device, but often
we want to send some command then wait for the device to react accordingly. The
`Device` class provides another convenience method called `wait_for_state` that
essentially wraps `assert_state` in a poll loop.  It accepts the same arguments
as `assert_state`, plus two more named arguments, `interval` and `timeout`.

#### head_call(...)

TODO

### Common Device Interface

`RealDevice` and `VirtualDevice` necessarily implement certain method
differently, but both adhere to an interface defined by `Device`.

#### Stopping and starting Beep services

TODO

#### ubus_call(...)

TODO

#### uci_set(...), uci_commit(), uci_get(...)

TODO

### beep.interface.RealDevice

TODO

#### Killing processes

TODO

#### Raw SSH commands

TODO

#### Evil AP integration

TODO

### beep.interface.VirtualDevice

TODO

## <a name="testcore"></a> Test Core

TODO
