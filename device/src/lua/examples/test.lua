#!/usr/bin/env lua

require "ubus"
require "uloop"

require 'beep_ubus'

local flags = require "flags"
local inspect = require 'inspect'
local log = require "log"

flags.add("awesome",false,nil)
flags.add("name",true,"Foobar")

flags.init(arg)

if flags.flags["awesome"] then
    log:info('Starting in awesome mode...')
end

log:info('Welcome, ' .. flags.flags['name'])

uloop.init()

local conn = beep_ubus_connect('lua_ubus_test')
if not conn then
    error("Failed to connect to ubus")
end

local receive = 1

local my_objects = {
    test1 = {
        echo = {
            function(req,msg)
                local def_req = conn:defer(req)
                log:info('Received ' .. receive)
                local this_receive = receive
                receive = receive + 1
                uloop.timer(
                    function()
                        conn:reply(def_req, beep_success(msg))
                        conn:complete_deferred(def_req)
                        log:info('Replied ' .. this_receive);
                    end, msg.delay)
            end, {delay = ubus.INT32, msg = ubus.STRING }
        },
        gc = {
            function(req, msg)
                log:info('Running garbage collection...');
                collectgarbage("collect");
            end, {unused = ubus.INT32}
        },
        broken = {
            function(req, msg)
                log:info('Sending beep error')
                conn:reply(req, beep_error())
            end, {unused = ubus.INT32}
        }
    }
}

conn:add(my_objects)
uloop.run()
