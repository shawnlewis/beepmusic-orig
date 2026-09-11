#!/usr/bin/env lua

require 'strict'

require 'beep_ubus'
require 'uloop'

local async = require 'async'
local flags = require 'flags'
local log = require 'log'

flags.init(arg)
log:init('async_example')

uloop.init()

--local conn = beep_ubus_connect('beepio')

local c_unbuf = async.Channel(async.Buffer())
local stop_channel = async.Channel(async.Buffer())

async.go(function()
    while true do
        local to = async.timeout(250)
        local ch = async.alts({to, stop_channel})
        if ch == stop_channel then
            break
        end

        async.timeout(250):take()
        c_unbuf:put(1)
    end
    log:info('Process 1 done')
end)

async.go(function()
    while true do
        async.timeout(1000):take()
        c_unbuf:put(2)
    end
end)

async.go(function()
    while true do
        async.timeout(1500):take()
        c_unbuf:put(3)
    end
end)

async.go(function()
    while true do
        local val = c_unbuf:take()
        log:info('got val: %s', val)
    end
end)


local conn = beep_ubus_connect('async_test')

objects = {}
objects['beep.async_test'] = {
    stop = beep_ubus_method(conn,
        function(req, msg)
            stop_channel:put(true)
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),
}

conn:add(objects)

async.start()

uloop.run()
