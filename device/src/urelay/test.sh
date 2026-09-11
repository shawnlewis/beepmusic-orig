#!/bin/bash

# To setup this test:
# cd ../out/host
# ubusd -s ./test.sock &
# ubusd -s ./test2.sock &
# ./urelay --ubus=./test.sock --urelay_port=15001
# ./urelay --ubus=./test2.sock --urelay_port=15002
# lua/examples/test.lua --ubus=./test2.sock

ubus -s ./test.sock -t 10 call urelay connect '{"ip":"127.0.0.1","port":15002,"id":"remote"}'

#--------------------
echo 'Mass Event Test'
ubus -s ./test.sock listen beep.state.* &
ubus_listen_pid=$!
for j in {1..5}
do
    for i in {1..5}
    do
        msg='{"event_type":"some_event", "event_data":{"a":"b","c":"d"}, "state":{"seq":'$i'}}'
        ubus -s ./test2.sock send beep.state.something._local_ "$msg" &
    done
    sleep .2
done
sleep 1
kill $ubus_listen_pid >/dev/null

#--------------------

#--------------------
echo 'Mass Echo Test'
for i in {1..50}
do
    ubus -s ./test.sock -t 3 call urelay invoke '{"id":"remote","path":"test1","method":"echo","msg":{"delay":2000,"msg":"__Mass Echo__"}}' &
done
sleep 4
#--------------------
echo 'Tier 2 Error Test'
ubus -s ./test.sock -t 3 call urelay invoke '{"id":"bad_id","path":"test1","method":"echo","msg":{"delay":2000,"msg":"__Oh, there you are!__"}}'
sleep 3

echo 'Tier 3 Error Test A (Object not found)'
ubus -s ./test.sock -t 3 call urelay invoke '{"id":"remote","path":"does.not.exist","method":"echo","msg":{"delay":2000,"msg":"__Oh, there you are!__"}}'
sleep 3

echo 'Tier 3 Error Test (Method on object not found)'
ubus -s ./test.sock -t 3 call urelay invoke '{"id":"remote","path":"test1","method":"does_not_exist","msg":{"delay":2000,"msg":"__Oh, there you are!__"}}'
sleep 3

echo 'Tier 4 Error Test'
ubus -s ./test.sock -t 3 call urelay invoke '{"id":"remote","path":"test1","method":"broken","msg":{"delay":2000,"msg":"__Oh, there you are!__"}}'
sleep 3

echo 'Disconnecting'
ubus -s ./test.sock -t 10 call urelay disconnect '{"id":"remote"}'
sleep 1

echo '--- End of tests ---'
