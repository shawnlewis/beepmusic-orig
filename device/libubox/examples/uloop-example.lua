#!/usr/bin/env lua

local uloop = require("uloop")

uloop.init()

-- timer example 1
local timer
function t()
	print("1000 ms timer run");
	timer:set(1000)
end
timer = uloop.timer(t)
timer:set(1000)

-- timer example 2
uloop.timer(function() print("2000 ms timer run"); end, 2000)

-- timer example 3
uloop.timer(function() print("3000 ms timer run"); end, 3000):cancel()

-- process
function p1(r)
	print("Process 1 completed")
	print(r)
end

function p2(r)
	print("Process 2 completed")
	print(r)
end

local proc1
local proc2

uloop.timer(
	function()
		proc1 = uloop.process("uloop_pid_test.sh", {"10", "foo", "bar"}, {"PROCESS=1"}, p1)
	end, 1000
)
uloop.timer(
	function()
		proc2 = uloop.process("uloop_pid_test.sh", {"15", "bar", "foo"}, {"PROCESS=2"}, p2)
	end, 2000
)

uloop.timer(
    function()
        print("P1 pid = " .. proc1:get_pid())
    end, 4000
)

uloop.timer(
    function()
        print("SIGTERM P2")
        proc2:sigterm()
    end, 5000
)
uloop.run()

