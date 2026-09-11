#!/bin/bash

ubus send beep.state.component1 '{"state":{"logged_in":false}}'
ubus send beep.state.component2 '{"state":{"varA":100}}'
ubus send beep.state.component3 '{"state":{"varA":-1, "varB":[1,2,3,4,5]}}'
sleep 2
ubus send beep.state.component1 '{"state":{"logged_in":true,"playing":false}}'
for i in {101..200}
do
    echo 'Incrementing component 2 varA state field to ' $i
    C2STATE='{"state":{"varA":'$i'}}'
    ubus send beep.state.component2 $C2STATE
    sleep 5
done
