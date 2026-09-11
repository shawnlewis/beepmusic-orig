#!/usr/bin/env lua

require "ubus"
require "uloop"

require 'beep_ubus'

local flags = require "flags"
local inspect = require 'inspect'
local log = require "log"

flags.init(arg)

uloop.init()

local conn = beep_ubus_connect('lua_ubus_test', 2)
if not conn then
    error("Failed to connect to ubus")
end

local ret1= conn:call_async("test1", "echo", {msg='hello', delay=1000},
    function(result, ubus_error_code)
        print('Result: ' .. inspect(result))
        print('ubus_error_code: ' .. inspect(ubus_error_code))
    end)

local ret1= conn:call_async("test1", "echo", {msg='hello', delay=5000},
    function(result, ubus_error_code)
        print('Result: ' .. inspect(result))
        print('ubus_error_code: ' .. inspect(ubus_error_code))
    end)

print('Call async return: ' .. inspect(ret))

uloop.run()
