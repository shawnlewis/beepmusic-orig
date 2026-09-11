#!/usr/bin/env lua

require "ubus"
require "uloop"

require 'beep_ubus'

local beep_player = require 'beep_player'
local inspect = require 'inspect'

uloop.init()

local conn = ubus.connect()
if not conn then
    error("Failed to connect to ubus")
end

local p = beep_player.new(conn, 'local', nil,
    function(pl)  -- on ready
        print('on local ready')
        pl:call('hello', nil,
            function(result, errors)
                print('Result: ' .. inspect(result))
                print('Errors: ' .. inspect(errors))
            end)
    end,
    function()  -- on event
        print('got player event')
    end)

uloop.run()
