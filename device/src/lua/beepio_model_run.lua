#!/usr/bin/lua
require 'beep_ubus'
require 'ubus'

local clock = require 'beepio_clock'
local flags = require 'flags'
local log = require 'log'

local beepio_model = require 'beepio_model'

flags.init(arg)
log:init('beepio')

clock.init()
local system_model = beepio_model.system_model

uloop.init()

local conn = beep_ubus_connect('beepio_model_run')

beepio_model.init(conn)

system_model:add_listener(function(event_name)
    local val
    local millis
    if event_name == 'mode_change' then
        val = system_model:get('mode')
        millis = system_model:get_change_millis('mode')
    elseif event_name == 'audio_state_change' then
        val = system_model:get('audio_state')
        millis = system_model:get_change_millis('audio_state')
    elseif event_name == 'volume_change' then
        val = system_model:get('local_volume')
        millis = system_model:get_change_millis('local_volume')
    end
    print(event_name .. ' ' .. val .. ' ' .. millis)
end)

uloop.run()
