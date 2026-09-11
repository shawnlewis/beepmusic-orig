module(..., package.seeall)

local log = require 'log'

local TICKS_PER_SECOND = 24
local TICK_INTERVAL_MILLIS = math.floor(1000 / TICKS_PER_SECOND)

local ticks = 0

local callbacks = {}

function tick()
    ticks = ticks + 1
    next_tick()
end

function next_tick()
    uloop.timer(tick, TICK_INTERVAL_MILLIS)
    for i, cb in ipairs(callbacks) do
        cb()
    end
end

function init()
    next_tick()
end

function millis()
    return ticks * TICK_INTERVAL_MILLIS
end

function add_tick_callback(cb)
    table.insert(callbacks, cb)
end

function remove_tick_callback(cb)
    for i, callback in ipairs(callbacks) do
        if cb == callback then
            table.remove(callbacks, i)
            return
        end
    end
end
