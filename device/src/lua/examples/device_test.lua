#!/usr/bin/env lua

require "ubus"
require "uloop"

require 'beep_ubus'

local device = require 'device'
local inspect = require 'inspect'
local flags = require 'flags'
local log = require "log"

flags.init(arg)

uloop.init()

local conn = beep_ubus_connect()

if not conn then
    error("Failed to connect to ubus")
end

local d_local = device.new(conn, 'local', nil, 'local',
    function()
        log:info('on local ready')
    end)

d_local:call('test1', 'echo', {delay=1, msg='Local echo'},
    function(result, errors)
        log:info('Result: ' .. inspect(result))
        log:info('Errors: ' .. inspect(errors))
    end)

-- d_local:listen('playnet',
--     function(event, msg)
--         log:info(event)
--         log:info(inspect(msg))
--     end)


local d_remote = device.new(conn, '127.0.0.1', 15002, '127_0_0_1__15002',
    function()
        log:info('on ready')
    end)

d_remote:call('test1', 'echo', {delay=500, msg='Remote echo'},
    function(result, errors)
        log:info('Result: ' .. inspect(result))
        log:info('Errors: ' .. inspect(errors))
    end)

--d_remote:listen('playnet',
--    function(event, msg)
--        log:info(event)
--        log:info(inspect(msg))
--    end)

uloop.run()
