****** SUPER IMPORTANT ******

Run init.sh and pass it a unique prefix before running these tests.  This changes
some critical values in each test device's uci config files as well as in the
system test script itself.

E.g.,

    ./init.sh foo_test

will set all test devices in this directory to be named foo_test_A, foo_test_B,
and so on.

Otherwise, you will trample other test devices and cause major issues with other
beeps/virtual beeps on the network.

System test
-----------

System test starts ten devices and sends random commands to them in an infinite
loop.  Monitor the Beep Clinic channel for fatal errors, or inspect log data
directly at:

    http://reverb.beepdevices.com/kibana

1. Snag a copy of the logstash jar:

    wget https://download.elasticsearch.org/logstash/logstash/logstash-1.2.1-flatjar.jar

2. Ensure you've run init.sh, as above.

3. ./runsystem.sh

Unit test
---------

Very basic tests against core components

1. ./runtest.sh
