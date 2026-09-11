#!/usr/bin/env lua

-- A ubus test
--
-- First start ubusd:
--     ubusd -s /tmp/a.ubus
--
-- Then either, start one:
--     ./ugrind.lua --ubus=/tmp/a.ubus --id=a
--
-- OR, start multiple:
--     ./ugrind.lua --ubus=/tmp/a.ubus --id=a --other_agents=b,c --action_interval_ms=10
--     ./ugrind.lua --ubus=/tmp/a.ubus --id=b --other_agents=a,c --action_interval_ms=10
--     ./ugrind.lua --ubus=/tmp/a.ubus --id=c --other_agents=a,b --action_interval_ms=10
--
-- If starting multiple they must be started within 2 seconds of eachother.
-- They may also fail if you don't slow it down with action_interval_ms,
-- because ubus gets too congested.
--
-- It's useful to filter the logs down to INFO, which is periodic status
-- information, ex:
--     ./ugrind.lua --ubus=/tmp/a.ubus --id=a  2>&1 | tee /tmp/a.txt | grep INFO

require 'beep_ubus'
require 'ubus'
require 'uloop'
require 'util'

local flags = require 'flags'
local inspect = require 'inspect'
local log = require 'log'

log.disable_repeat_suppression()

require 'strict'

flags.add('id', true, 'a')
flags.add('other_agents', true)
flags.add('action_interval_ms', true, 1)

flags.init(arg)

OTHER_AGENTS = nil
if flags.flags.other_agents then
    OTHER_AGENTS = string_split_by(flags.flags.other_agents, ',')
end

uloop.init()
local conn = beep_ubus_connect('ugrind')

local listeners = {}

local action_count = 0
local top_object_id = 1
local call_val = 1

function get_object_name(id, prog_id)
    return string.format('object_%s_%s', prog_id or flags.flags.id, id)
end

function add_object(object_name)
    log:debug('Adding object: %s', object_name)
    local objects = {}
    objects[object_name] = {
        method = {
            function(req, msg)
                log:debug('Received method call for object: %s', object_name)
                if msg.defer then
                    local deferred_req = conn:defer(req)
                    uloop.timer(function()
                        conn:reply(deferred_req, {intval = msg.intval})
                        conn:complete_deferred(deferred_req)
                    end, msg.defer)
                elseif not msg.dont_respond then
                    conn:reply(req, {intval = msg.intval})
                else
                    conn:defer(req)
                end
            end, {intval = ubus.INT32, dont_respond = ubus.INT32,
                defer = ubus.INT32}
        }
    }
    conn:add(objects)
end

function action_add_object()
    local object_name = get_object_name(top_object_id)
    add_object(object_name)
    top_object_id = top_object_id + 1
end

local call_method_seq = 1

function regular_call(object_name)
    log:debug('Calling method on object: %s', object_name)

    call_val = call_val + 1
    local cur_call_val = call_val

    call_method_seq = call_method_seq + 1
    local cur_call_method_seq = call_method_seq

    local ubus_result = conn:call_async(object_name, 'method',
        {intval = cur_call_val},
        function(ubus_result, ubus_error_code)
            log:debug('Received ubus_result, ubus_error_code: %s %s',
                    ubus_result or 'nil', ubus_error_code or 'nil')
            if ubus_error_code then
                log:error('Got unexpected ubus_error: %s', ubus_error_code)
                os.exit(1)
            end
            if cur_call_val ~= ubus_result.intval then
                log:error('Got wrong call_val: %s', cur_call_val)
                os.exit(1)
            end
            do_random_action()
        end, 1000)
    if ubus_result then
        log:error('  Got ubus result: %s', ubus_result)
        os.exit(1)
    end
end

function action_call_method()
    if top_object_id <= 1 then
        -- haven't added any objects yet.
        return
    end
    local object_name = get_object_name(math.random(top_object_id - 1))
    regular_call(object_name)
end

local call_method_no_response_seq = 1
function action_call_method_no_response()
    if top_object_id <= 1 then
        -- haven't added any objects yet.
        return
    end
    local object_name = get_object_name(math.random(top_object_id - 1))
    log:debug('no_response Calling method on object: %s', object_name)

    local ubus_result = conn:call_async(object_name, 'method',
        {intval = 5, dont_respond = 1},
        function(ubus_result, ubus_error_code)
            log:debug('no_response Received ubus_result, ubus_error_code: %s %s',
                    ubus_result or 'nil', ubus_error_code or 'nil')
            if ubus_result then
                log:error('Got unexpected ubus_response: %s', ubus_result)
                os.exit(1)
            end
            if ubus_error_code ~= 7 then
                log:error('Got unexpected ubus_error: %s', ubus_error_code)
                os.exit(1)
            end
            do_random_action()
        end, 1000)
    if ubus_result then
        log:error('  Got ubus result: %s', ubus_result)
        os.exit(1)
    end
end

local call_method_no_response_seq = 1
function action_call_method_deferred_response()
    if top_object_id <= 1 then
        -- haven't added any objects yet.
        return
    end
    local object_name = get_object_name(math.random(top_object_id - 1))
    log:debug('deferred Calling method on object: %s', object_name)

    call_val = call_val + 1
    local cur_call_val = call_val

    local ubus_result = conn:call_async(object_name, 'method',
        {intval = cur_call_val, defer = 1000},
        function(ubus_result, ubus_error_code)
            log:debug('deferred Received ubus_result, ubus_error_code: %s %s',
                    ubus_result or 'nil', ubus_error_code or 'nil')
            if ubus_error_code then
                log:error('Got unexpected ubus_error: %s', ubus_error_code)
                os.exit(1)
            end
            if cur_call_val ~= ubus_result.intval then
                log:error ('Got wrong call_val: ', cur_call_val)
                os.exit(1)
            end
            do_random_action()
        end, 2000)
    if ubus_result then
        log:error('  Got ubus result: %s', ubus_result)
        os.exit(1)
    end
end

local call_method_no_response_seq = 1
function action_call_method_late_response()
    if top_object_id <= 1 then
        -- haven't added any objects yet.
        return
    end
    local object_name = get_object_name(math.random(top_object_id - 1))
    log:debug('late_response Calling method on object: ' .. object_name)

    local ubus_result = conn:call_async(object_name, 'method',
        {intval = 5, defer = 2000},
        function(ubus_result, ubus_error_code)
            log:debug('late_response Received ubus_result, ubus_error_code: %s %s',
                    ubus_result or 'nil', ubus_error_code or 'nil')
            if ubus_result then
                log:error('Got unexpected ubus_response: %s', ubus_result)
                os.exit(1)
            end
            if ubus_error_code ~= 7 then
                log:error('Got unexpected ubus_error: %s', ubus_error_code)
                os.exit(1)
            end
            do_random_action()
        end, 1000)
    if ubus_result then
        log:error('  Got ubus result: %s', ubus_result)
        os.exit(1)
    end
end

function action_listen()
    log:debug('Adding a listener.')
    local listener
    listener = conn:listen('event',
        function()  -- on event
            log:debug('Got event')
            if math.random(100) < 10 then
                do_random_action()
            end
        end,
        function()  -- on added
            log:debug('Added listener.')
            log:debug('   listener index: %s', #listeners)
            log:debug('   listener val: %s', tostring(listener))
            table.insert(listeners, listener)
        end)
    log:debug('   listener val: %s', tostring(listener))
end

function action_remove_listener()
    log:debug('Removing a listener.')
    if #listeners == 0 then
        return
    end
    local index = math.random(#listeners)
    log:debug('   listener index: %s', index)
    log:debug('   listener val: %s', tostring(listeners[index]))
    conn:cancel_listen(listeners[index])
    table.remove(listeners, index)
end

timers = {}

function action_add_timer()
    log:debug('Adding timer')
    local timer
    timer = uloop.timer(function()
        log:debug('Timer callback')
        for i, list_timer in ipairs(timers) do
            if timer == list_timer then
                table.remove(timers, i)
                break
            end
        end
        do_random_action()
    end, math.random(1000))
    table.insert(timers, timer)
end

function action_remove_timer()
    log:debug('Removing timer')
    if #timers == 0 then
        return
    end
    local index = math.random(#timers)
    timers[index]:cancel()
    table.remove(timers, index)
end

function action_send_event()
    log:debug('Sending event')
    conn:send_event('event', {c = 6})
end

function action_call_other_agent()
    local other_agent = OTHER_AGENTS[math.random(1, #OTHER_AGENTS)]
    regular_call(get_object_name('base', other_agent))
end

local actions = {action_add_object,
                 action_call_method,
                 action_call_method_deferred_response,
                 action_call_method_late_response,
                 action_call_method_no_response,
                 action_add_timer,
                 action_remove_timer,
                 action_listen,
                 action_remove_listener,
                 action_send_event}

function do_random_action()
    action_count = action_count + 1
    local action = actions[math.random(#actions)]
    action()
end

function base_timer()
    do_random_action()
    uloop.timer(base_timer, flags.flags.action_interval_ms)
end

function info_timer()
    log:info('actions: %s, objects: %s, listeners: %s, timers: %s',
            action_count, top_object_id, #listeners, #timers)
    uloop.timer(info_timer, 500)
end

add_object(get_object_name('base'))

if OTHER_AGENTS then
    -- wait a few seconds to give other agents time to start
    uloop.timer(function()
        table.insert(actions, action_call_other_agent)
    end, 2000)
end

base_timer()
info_timer()

uloop.run()
