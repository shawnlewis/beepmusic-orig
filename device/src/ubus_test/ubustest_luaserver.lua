#!/usr/bin/env lua

require 'ubus'
require 'uloop'

require 'beep_ubus'
require 'util'

require 'beep'

local flags = require 'flags'
local log = require 'log'

require 'strict'

----- on startup

flags.init(arg)
log:init('ubustest_luaserver')

uloop.init()

-- connect to ubus
local conn = beep_ubus_connect('distributor')

-- ubus object definition
local objects = {}
objects['ubustest_luaserver'] = {
    handle_data = beep_ubus_method(conn,
        function(req, msg)
            local size = string.len(msg['data'])
            log:info('handle_data called with size: %s', size)
            beep_reply(conn, req, beep_success())
        end,
        {data=ubus.STRING}
    ),
}

---- on startup

conn:add(objects)

uloop.run()
