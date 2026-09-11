#!/bin/sh

was_down=no
sample_interval=`uci get beep.main.pinger_interval`
if [ -z "$sample_interval" ]
then
    sample_interval=15
    logger -s -t "pinger" "pinger_interval option not set in /etc/config/beep"
    logger -s -t "pinger" "using default value: $sample_interval"
fi

while [ 1 ]; do
	next_iter_time=`expr \`date +%s\` + $sample_interval`
    router_ip=`route -n | grep ^0.0.0.0 | awk '{print $2}'`
	ping -c 1 -w $sample_interval $router_ip
	if [ $? -ne 0 ]; then
		logger -s -t "pinger" "Gateway unreachable"
		was_down=yes
	else
		if [ $was_down == "yes" ]; then
			logger -s -t "pinger" "Gateway restored.  Restarting beep."
			killall beepdiscovery	
		fi
		was_down=no
		now=`date +%s`
		sleep `expr $next_iter_time - $now`
	fi
done
