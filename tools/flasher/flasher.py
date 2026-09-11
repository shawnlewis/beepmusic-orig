#!/usr/bin/python

import commands
import pickle
import random
import re
import serial
import sys
import time

class WaitError(Exception):
    pass


class BeepSerialMixin(object):
    def log(self, s):
        print s

    def wait_for_string(self, regex, timeout):
        self.log('Waiting %ss for string: %s' % (timeout, regex))
        buf = ''
        self.timeout = 0

        start_time = time.time()

        while 1:
            elapsed = time.time() - start_time
            if elapsed > timeout:
                break

            chars = self.read(80)
            #print chars
            buf += chars
            buf = buf[-1024:]

            match = re.search(regex, buf)
            if match:
                self.log('   Success.')
                return match

            time.sleep(.05)

        self.log('ERROR: Timed out waiting for line: %s' % regex)
        self.log('Current buffer:')
        self.log(buf)
        sys.exit(1)

    def waitForLine(self, regex, timeout):
        self.log('Waiting %ss for line: %s' % (timeout, regex))
        start_time = time.time()
        self.timeout = timeout

        if not getattr(self, '_lines', None):
            self._lines = []

        while 1:
            elapsed = time.time() - start_time
            if elapsed > timeout:
                break

            self.timeout = timeout - elapsed
            line = self.readline()
            #print 'LINE: ', line
            if not line:
                break

            self._lines.append(line)

            match = re.search(regex, line)
            if match:
                self.log('   Success.')
                return match

        self.log('ERROR: Timed out waiting for line: %s' % regex)
        self.log('Last 10 lines:')
        for line in self._lines[-10:]:
            self.log(line)
        raise WaitError

    def writelines(self, lines):
        # HACK, wait for whatever the last command was to finish, otherwise
        # sometimes we don't send the full line.
        time.sleep(1)
        lines = ['%s\n' % l for l in lines]
        super(BeepSerialMixin, self).writelines(lines)

class BeepSerial(BeepSerialMixin, serial.Serial):
    pass

class FakeSerial(BeepSerialMixin):
    def __init__(self, *args, **kwargs):
        self._script = []

    def addScriptLine(self, line):
        self._script.append(line)

    def readline(self):
        if self._script:
            return self._script.pop(0)
        else:
            print
            print
            print 'Script ran out of lines!'
            sys.exit(1)

    def waitForLine(self, regex, timeout):
        print 'Waiting for: %s' % regex
        return super(FakeSerial, self).waitForLine(regex, timeout)

    def writelines(self, lines):
        pass

def run_stm(stm_ser, stm_script):
    time.sleep(2)
    for wr, rd, to in stm_script:
        #print(repr(wr), repr(rd), to)
        stm_ser.setTimeout(to)
        stm_ser.write(wr)
        buf = stm_ser.read(len(rd))
        if buf != rd:
            print('stm flash error: expected {} read {}'.format(
                    repr(rd), repr(buf)))
            # Might as well flash the module.
            return

def run_module(ser, image_ip, image_name, stm_ser=None, stm_script=None):
    # 0) wait for startup
    ser.waitForLine('U-Boot', 30)

    # There is plenty of time during the Carambola OpenWrt boot to program
    # the stm.
    if stm_ser is not None and stm_script is not None:
        print('Flashing stm')
        run_stm(stm_ser, stm_script)

    ## 1) wait til jffs2 is done, takes about 65 seconds
    #match = ser.waitForLine('jffs2_build_xattr_subsystem: complete building xattr subsystem, (\d+) of xdatum \((\d+) unchecked, (\d+) orphan\) and (\d+) of xref \((\d+) dead, (\d+) orphan\) found\.', 90)
    #match_vals = match.groups()

    ## we expect each of the values matched in the regex above to be zero
    #for v in match_vals:
    #    if int(v) != 0:
    #        print 'Got invalid value for jffs2 build completion: %s' % v

    #match = ser.waitForLine('entered promiscuous mode', 90)
    match = ser.waitForLine('jffs2_build_xattr_subsystem: complete building xattr subsystem, (\d+) of xdatum \((\d+) unchecked, (\d+) orphan\) and (\d+) of xref \((\d+) dead, (\d+) orphan\) found\.', 90)
    #match_vals = match.groups()

    time.sleep(3)

    # make sure console is started
    ser.writelines(['echo', 'echo'])

    time.sleep(2)

    # 2) write network settings
    ser.writelines([
        'uci set wireless.radio0.disabled=0',
        'uci set wireless.@wifi-iface[0].network=wifi',
        'uci set wireless.@wifi-iface[0].mode=sta',
        'uci set wireless.@wifi-iface[0].ssid=YOUR_WIFI_SSID',
        'uci set wireless.@wifi-iface[0].encryption=psk2',
        'uci set wireless.@wifi-iface[0].key=YOUR_WIFI_PASSWORD',
        'uci set network.wifi=interface',
        'uci set network.wifi.proto=dhcp',
        'uci commit',
        'echo ***DONE***'
    ])
    try:
        ser.waitForLine('\*\*\*DONE\*\*\*', 5)
    except WaitError:
        ser.writelines([
            'uci set wireless.radio0.disabled=0',
            'uci set wireless.@wifi-iface[0].network=wifi',
            'uci set wireless.@wifi-iface[0].mode=sta',
            'uci set wireless.@wifi-iface[0].ssid=YOUR_WIFI_SSID',
            'uci set wireless.@wifi-iface[0].encryption=psk2',
            'uci set wireless.@wifi-iface[0].key=YOUR_WIFI_PASSWORD',
            'uci set network.wifi=interface',
            'uci set network.wifi.proto=dhcp',
            'uci commit',
            'echo ***DONE***'
        ])
        ser.waitForLine('\*\*\*DONE\*\*\*', 5)

    time.sleep(1)

    ser.writelines(['tail -n +2 /etc/shadow > /tmp/shadow'])
    time.sleep(1)
    ser.writelines(['echo root:\$1\$lvtsCtut\$.UdM5ZCclBuVUgW3lCl/q0:16395:0:99999:7::: >> /tmp/shadow'])
    time.sleep(1)
    ser.writelines(['cp /tmp/shadow /etc/shadow'])
    time.sleep(1)

    # STOP here, we'll do the rest of the flash later.
    return


    # random sleep to stagger network connection across devices
    time.sleep(random.random() * 5)

    # 3) cycle network
    ser.writelines(['/etc/init.d/network stop'])
    #ser.waitForLine('deauthenticating', 10)
    time.sleep(1)
    ser.writelines(['/etc/init.d/network start'])
    try:
        ser.waitForLine('associated', 30)
    except WaitError:
        print 'WIFI DID NOT ASSOCIATE, RESTARTING NETWORK'
        ser.writelines(['/etc/init.d/network restart'])
        try:
            ser.waitForLine('associated', 30)
        except WaitError:
            print '*** WIFI DID NOT ASSOCIATE AGAIN!, REBOOTING'
            ser.writelines(['reboot'])
            ser.waitForLine('associated', 30)

    ## 4) flash stm app
    #ser.writelines([
    #    'cd /tmp',
    #    'wget http://10.0.1.21/ioutil'
    #])
    #ser.waitForLine('ioutil.*100%', 10)
    #ser.writelines([
    #    'wget http://10.0.1.21/controller_0006.bin'
    #])
    #ser.waitForLine('controller_0006.bin.*100%', 10)

    ## run ioutil
    #ser.writelines([
    #    'chmod 755 ioutil',
    #    './ioutil stop'
    #])
    #ser.waitForLine('command.*returned code.*', 10)
    #ser.writelines([
    #    './ioutil write-fw controller_0006.bin 0x0006'
    #])
    #ser.waitForLine('command.*returned code 0', 10)
    #ser.writelines([
    #    './ioutil autoboot 1'
    #])
    #ser.waitForLine('command.*returned code 0', 10)
    #ser.writelines([
    #    './ioutil start'
    #])
    #ser.waitForLine('command.*returned code 0', 10)

    # Allow DHCP to get an ip address.
    time.sleep(10)

    # 5) install beep image
    ser.writelines([
        'cd /tmp',
        'wget http://{}/{}'.format(image_ip, image_name)
    ])
    try:
        ser.waitForLine('beep.*bin.*100%', 40)
    except WaitError:
        # kick the network and try again
        print '** Didn\'t download image, kicking network and trying again'
        ser.writelines(['/etc/init.d/network restart'])
        try:
	    ser.waitForLine('associated', 30)
	    time.sleep(10)
	    ser.writelines([
	        'cd /tmp',
	        'wget http://{}/{}'.format(image_ip, image_name)
	    ])
            ser.waitForLine('beep.*bin.*100%', 40)
        except WaitError:
            print '***** Didn\'t download image, kicking network and trying again'
            ser.writelines(['/etc/init.d/network restart'])
            ser.waitForLine('associated', 30)
	    time.sleep(10)
            ser.writelines([
                'cd /tmp',
                'wget http://{}/{}'.format(image_ip, image_name)
            ])
            ser.waitForLine('beep.*bin.*100%', 40)

    # takes about 120 seconds
    ser.writelines([
        'sysupgrade -v -n /tmp/{}'.format(image_name)
    ])
    ser.waitForLine('Performing system upgrade', 20)
    ser.waitForLine('Rebooting', 120)
    match = ser.waitForLine('jffs2_build_xattr_subsystem: complete building xattr subsystem, (\d+) of xdatum \((\d+) unchecked, (\d+) orphan\) and (\d+) of xref \((\d+) dead, (\d+) orphan\) found\.', 150)
    match_vals = match.groups()

    # we expect each of the values matched in the regex above to be zero
    for v in match_vals:
        if int(v) != 0:
            print 'Got invalid value for jffs2 build completion: %s' % match_vals


def module_main(argv):
    #ser = BeepSerial('/dev/tty.usbserial-FTGGE7MJ', 115200)
    if len(argv) < 6:
        print('usage: flasher.py <module_serial_device> <stm_serial_device> <image ip> <image name> <upgrade_uboot> [stm script]')

    if len(argv) > 6:
        stm_ser = BeepSerial(argv[2], baudrate=115200,
                parity=serial.PARITY_EVEN)
        stm_script_file = open(argv[6], 'r')
        stm_script = pickle.load(stm_script_file)
        stm_script_file.close()
    else:
        stm_ser = None
        stm_script = None
    ser = BeepSerial(argv[1], 115200)
    if argv[5] == '1':
        upgrade_uboot_carambola_to_beep(ser)
    run_module(ser, argv[3], argv[4], stm_ser, stm_script)

def module_test():
    ser = FakeSerial()
    ser.addScriptLine('U-Boot 1.1.4-g33f82657-dirty (Sep 16 2013 - 16:09:28)')
    ser.addScriptLine('jffs2_build_xattr_subsystem: complete building xattr subsystem, 0 of xdatum (0 unchecked, 0 orphan) and 0 of xref (0 dead, 0 orphan) found.')

    # UCI
    ser.addScriptLine('***DONE***')

    # network cycle
    ser.addScriptLine('[ 2523.610000] wlan0: deauthenticating from f8:1a:67:33:05:13 by local choice (reason=3)')
    ser.addScriptLine('[ 2635.430000] wlan0: authenticate with f8:1a:67:33:05:13')
    ser.addScriptLine('[ 2635.460000] wlan0: send auth to f8:1a:67:33:05:13 (try 1/3)')
    ser.addScriptLine('[ 2635.470000] wlan0: authenticated')
    ser.addScriptLine('[ 2635.480000] wlan0: associate with f8:1a:67:33:05:13 (try 1/3)')
    ser.addScriptLine('[ 2635.480000] wlan0: RX AssocResp from f8:1a:67:33:05:13 (capab=0x411 status=0 aid=2)')
    ser.addScriptLine('[ 2635.490000] wlan0: associated')


    # downloadling ioutil/image
    ser.addScriptLine('Connecting to 10.0.1.21 (10.0.1.21)')
    ser.addScriptLine('ioutil  100% |*******************************|  5253   0:00:00 ETA')
    ser.addScriptLine('root@beep-00dca6:/tmp#')
    ser.addScriptLine('Connecting to 10.0.1.21 (10.0.1.21)')
    ser.addScriptLine('controller_0006.bin  100% |*******************************|  5253   0:00:00 ETA')
    ser.addScriptLine('root@beep-00dca6:/tmp#')

    # ioutil results
    ser.addScriptLine('Success: command 10 returned code 2')
    ser.addScriptLine('Success: command 1 returned code 0')
    ser.addScriptLine('Success: command 2 returned code 0')
    ser.addScriptLine('Success: command 3 returned code 0')

    # downloading beep image
    ser.addScriptLine('Connecting to 10.0.1.21 (10.0.1.21)')
    ser.addScriptLine('beep-v0.6.3.bin  100% |*******************************|  5253   0:00:00 ETA')
    ser.addScriptLine('root@beep-00dca6:/tmp#')
    ser.addScriptLine('Sending KILL to remaining processes ...')
    ser.addScriptLine('Switching to ramdisk...')
    ser.addScriptLine('Performing system upgrade...')
    ser.addScriptLine('Unlocking firmware ...')
    ser.addScriptLine('')
    ser.addScriptLine('Writing from <stdin> to firmware ...')
    ser.addScriptLine('Upgrade completed')
    ser.addScriptLine('Rebooting system...')

    # a bunch of stuff happens, and then:
    ser.addScriptLine('jffs2_build_xattr_subsystem: complete building xattr subsystem, 0 of xdatum (0 unchecked, 0 orphan) and 0 of xref (0 dead, 0 orphan) found.')

    run_module(ser, '10.0.1.21', 'beep-v0.6.3.bin')


def enter_uboot(ser, prompt):
    # Get into u-boot by spamming key-presses
    read_vals = ''
    start_time = time.time()
    print 'Trying to enter uboot'
    while 1:
        if time.time() - start_time > 600:
            print 'Timed out waiting for U-Boot :('
            sys.exit(1)
        ser.write('\x1b')
        ser.timeout = 0
        chars = ser.read(80)
        #print chars
        read_vals += chars
        if prompt in read_vals:
            break
        time.sleep(0.1)

    print 'Entered uboot'
    ser.write('\n')
    ser.wait_for_string(prompt, 2)

def upgrade_uboot_carambola_to_beep(ser):
    prompt = 'ar7240>'
    enter_uboot(ser, prompt)

    print 'Entering ymodem receive mode'
    ser.writelines(['loady'])
    ser.wait_for_string('Ready for binary', 3)
    time.sleep(1)

    print 'Sending file'
    s, o = commands.getstatusoutput('./ymodemsend.sh %s uboot_beep_cm2.bin' % ser.name)
    print 'ymodemsend.sh output: ', s, o
    if s != 0:
        print 'non-zero exit status for ymodemsend.sh, exiting'

    ser.wait_for_string(prompt, 30)

    ser.writelines(['erase 0x9f000000 +0x40000'])
    ser.wait_for_string(prompt, 30)

    ser.writelines(['cp.b 0x81000000 0x9f000000 0x40000'])
    ser.wait_for_string(prompt, 30)

    ser.writelines(['reset'])

    # now prompt changes to the new uboot prompt
    prompt = 'uboot>'
    enter_uboot(ser, prompt)

    ser.writelines(['saveenv'])
    ser.wait_for_string(prompt, 30)

    ser.writelines(['reset'])


def main(argv):
    start_time = time.time()
    print '*** Beep flasher ***'
    print
    print 'Please plug a Beep device in within 600 seconds'
    print
    module_main(argv)
    #ser = BeepSerial('/dev/tty.usbserial-FTGGE4NK', 115200)
    #upgrade_uboot_carambola_to_beep(ser, None)
    print 'Elapsed seconds: %s' % (time.time() - start_time)


if __name__ == '__main__':
    main(sys.argv)
