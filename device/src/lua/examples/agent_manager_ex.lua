#!/usr/bin/lua

require 'ubus'
require 'uloop'

require 'agent_manager'
local inspect = require 'inspect'
local log = require 'log'

uloop.init()
local conn = ubus.connect()
if not conn then
    error("Failed to connect to ubus")
end

AgentManager:init(conn)

AgentManager:start_agent(
        "lua examples/agent_manager_test_app.lua", "beep.app.test", 3000,
    function ()  -- on_added
        log:info('agent started')
    end,
    function ()  -- on_start_timeout
        log:error('agent timed out')
    end,
    function ()  -- on_removed
        log:info('agent removed')
    end)

uloop.run()
