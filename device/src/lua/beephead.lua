#!/usr/bin/env lua

require 'ubus'
require 'uloop'
require 'util'

require 'beep_ubus'
require 'beep'

local device = require 'device'
local flags = require 'flags'
local log = require 'log'

local json = require 'JSON'

local beephead_remotes = require 'beephead_remotes'

flags.add('publish_ubus_events',false)

flags.init(arg)
log:init('beephead')

uloop.init()

local MAX_ENTRIES = 20
local API_VERSION = 1

local conn = beep_ubus_connect('beephead')

local call_num = 1

-- starts at 2, so that a requested seq of 0 can force a refresh, but we
-- will respond with seq 1, which will block until the first update (2)
-- comes in.
local cur_seq = 2
local near_seq = 1

local parked_requests = {}

-- recent_events is a sliding array of state-changine events, starting at
-- 1 then growing monotonically while removing events that are MAX_ENTRIES
-- behind the "head"
local recent_events = {}

-- latest_states is a table of the most recent states for all objects in the
-- cluster.  It is keyed by the following scheme:
--
-- "group_id.object"
local latest_states = {}

-- translates a list of beep event objects into a response appropriate for
-- controllers.
local function format_response(raw_events, is_refresh)
    local group_events = {_force_object_hack=true}

    for _, event in ipairs(raw_events) do
        key = 'group.' .. event.group_id

        group_events[key] = group_events[key] or {}

        local group_update = {
            object = event.object,
            event_type = event.event_type,
            event_data = event.event_data,
            state = event.state
        }

        -- TODO: audio objects need group_update.state.group_instance_id
        table.insert(group_events[key], group_update)
    end

    return {
        is_refresh = is_refresh or false,
        seq = cur_seq - 1,
        data = group_events
    }
end

-- Sends the most recent event to anyone currently waiting.
local function notify_parked_requests(event)
    for i, v in ipairs(parked_requests) do
        beep_reply(
                conn,
                v.request,
                beep_success(format_response({event})))
    end
    parked_requests = {}
end

local PURGE_INTERVAL = 5
local STALE_THRESHOLD = 60
function _purge_stale_requests()
    local now = os.time()
    for i,v in ipairs(parked_requests) do
        if v.stamp <= now - STALE_THRESHOLD then
            -- If beephead-based clients start acting strangely, it might be
            -- because we are purging their requests and they aren't getting
            -- the updates they expect (though, AFAIK, all clients implement
            -- a shorter timeout).  But just in case, might be useful to
            -- announce when these requests are being discarded
            -- log:info('Discarding stale request!')
            beep_reply(conn, v.request,
                    beep_error('Request timed out', UBUS_STATUS_TIMEOUT))
            table.remove(parked_requests, i)
        end
    end

    uloop.timer(_purge_stale_requests, PURGE_INTERVAL * 1000)
end

uloop.timer(_purge_stale_requests, PURGE_INTERVAL * 1000)

-- Add event types to blacklist in this function

local blacklisted_events = {}
blacklisted_events['progress'] = true

local function is_blacklisted(event_type)
    -- Avoid the fake ternary operators when dealing with boolean outputs
    if blacklisted_events[event_type] then
        return true
    else
        return false
    end
end

local function cull_agent_for_group(group_id, agent)
    log:debug('Culling agent %s.%s', group_id, agent)
    local key = group_id .. '.' .. agent
    latest_states[key] = nil
end

local function cull_agents_for_group(group_id)
    for key,agent in pairs(latest_states) do
        if agent.group_id == group_id then
            latest_states[key] = nil
        end
    end
end

local function increment_sequence()
    cur_seq = cur_seq + 1

    if near_seq < (cur_seq - MAX_ENTRIES) then
        near_seq = near_seq + 1
    end

end

local removed_groups = {}

-- all incoming events should be added to the recent_events buffer using this
-- function, and the 
local function process_event(group_id, object, event_type, event_data, state)
    if not is_blacklisted(event_type) then
        log:info('Update state #%d: %s:%s %s',
                cur_seq, group_id, object, event_type or '(nil)')
    end

    if removed_groups[group_id] and event_type ~= 'group_added' then
        log:info('Ignoring %s:%s:%s', group_id, object, event_type or 'nil')
        return
    end

    local event = {}

    event.seq = cur_seq
    event.group_id = group_id
    event.object = object
    event.event_type = event_type
    event.event_data = event_data
    event.state = state

    recent_events[cur_seq] = event
    recent_events[cur_seq - MAX_ENTRIES] = nil

    if event_type == 'group_removed' then
        removed_groups[group_id] = true
        cull_agents_for_group(group_id)
        increment_sequence()
        notify_parked_requests(event)
        return
    elseif
        event_type == 'app_removed' then
        cull_agent_for_group(group_id, object)
        increment_sequence()
        notify_parked_requests(event)
        return
    elseif event_type == 'group_added' then
        removed_groups[group_id] = nil
    end

    -- The groups table only contains state, no events.
    local key = event.group_id .. '.' .. event.object

    latest_states[key] = {
        group_id = group_id,
        object = object,
        state = state
    }

    -- increment this before calling conn:send_event, otherwise we could get
    -- multiple events with the same seq due to ubus recursion.
    increment_sequence()
    notify_parked_requests(event)

    if flags.flags['publish_ubus_events'] then
        conn:send_event('beep.update', {})
    end
end

local function on_remote_event(group_id, object, event_type, event_data, state)
    process_event(group_id, object, event_type, event_data, state)
end

-- ### ubus interface ###

-- echo equivalent boom statement for a beep.head:call to log:debug
local function echo_call(msg)
    local call_str =
        'boom ubus call beep.head call \'' ..
        json:encode(msg) .. '\''
    log:debug(call_str)
end

-- translates from controller object name to agent name
local function translate_object(object_name)
    local agent_name = nil
    if object_name == 'audio' then
        agent_name = 'distributor'
    elseif object_name == 'manager' or object_name == 'cloud'
            or string_starts(object_name, 'app.') then
        agent_name = object_name
    end

    if not agent_name then
        log:error('Could not translate object name: ' .. object_name)
    end
    return agent_name
end

local remote_manager = {}
-- Pass nil as local device's error handler because all error logic is handled
-- in the done callback
local local_dev = device.new(conn, 'beephead', true, 'local', nil, 'local', nil)

local objects = {}
objects['beep.head'] = {
    hello = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success({api_version = API_VERSION}))
        end, {__unused = ubus.STRING}
    ),

    call = beep_ubus_method(conn,
        function(req, msg)

            -- Disabled for performance reasons, the json:encode is expensive
            -- on the device.
            -- echo_call(msg)

            -- Validate context. Should be 'group.<group_id>' or 'system'
            local dot_pos = string.find(msg.context, '%.')
            if not dot_pos and msg.context ~= 'system' then
                local error_msg = 'Received unexpected context ' ..
                        msg.context
                log:error(error_msg)
                beep_reply(conn, req, beep_error(error_msg))
                return
            end
            local group_id = ((msg.context == 'system') and 'system') or
                    string.sub(msg.context, dot_pos+1, -1)

            -- Route non-system calls to the right source
            if group_id ~= 'system' then
                local interface = remote_manager:get_group_interface(group_id)
                if not interface then
                    local error_msg = 'Invalid group ID ' .. group_id
                    log:error(error_msg)
                    beep_reply(conn, req, beep_error(error_msg))
                    return
                end

                local agent_name = translate_object(msg.object)
                if not agent_name then
                    local error_msg = 'Invalid object name ' .. msg.object
                    log:error(error_msg)
                    beep_reply(conn, req, beep_error(error_msg))
                    return
                end

                local call_num_upvalue = call_num
                call_num = call_num + 1

                local call_start_h, call_start_l = beep.beep_millis()
                log:debug('Calling ' .. agent_name .. ':' .. msg.method
                        .. ' ' .. call_num_upvalue)
                interface:call('beep.' .. agent_name, msg.method, msg.params,
                    function(result, errors)
                        if result then
                            local call_end_h, call_end_l = beep.beep_millis()
                            local _, time_delta = beep.beep_millis_sub(
                                    call_end_h, call_end_l,
                                    call_start_h, call_start_l)
                            log:debug('Call done ' .. agent_name .. ':' ..
                                    msg.method .. ' ' .. call_num_upvalue ..
                                    ' time delta: ' .. time_delta)
                            beep_reply(conn, req, beep_success(result))
                        else
                            beep_reply(conn, req,
                                    beep_error(errors.beep_error_message,
                                               errors.beep_error_code))
                        end
                    end)
            else
                -- system calls are routed to the local beepmanager or
                -- beepcloud
                local agent_name = translate_object(msg.object)

                local system_call_cb = function(result, errors)
                    if result then
                        beep_reply(conn, req, beep_success(result))
                    else
                        beep_reply(conn, req,
                                beep_error(errors.beep_error_message,
                                           errors.beep_error_code))
                        if errors.ubus_error_code ~= nil and
                                errors.ubus_error_code == 4 then
                            log:error('Fatal error: Manager object not found!' ..
                                    ' Exiting...')
                            uloop.cancel()
                        end
                    end
                end

                -- manager and cloud agents are passed directly to
                -- those services.  app.* agents are actually routed through
                -- beepmanager via the integration_rpc method, because those
                -- apps may be running on a different device
                if agent_name == 'manager' or agent_name == 'cloud' then
                    local_dev:call('beep.' .. agent_name, msg.method,
                            msg.params, system_call_cb)
                elseif agent_name ~= nil and
                        string_starts(agent_name, 'app.') then
                    local rpc_params = {
                            name = agent_name,
                            method = msg.method,
                            params = msg.params}

                    local_dev:call('beep.manager', 'integration_rpc',
                            rpc_params, system_call_cb)
                else
                    local error_msg =
                            'system context only supports manager, cloud ' ..
                            'and app.* objects'
                    log:error(error_msg)
                    beep_reply(conn, req, beep_error(error_msg))
                    return
                end
            end
            -- need to make sure we are connected to remotes before
            -- calling
        end, {object = ubus.STRING, context = ubus.STRING,
              method = ubus.STRING, params = ubus.TABLE}
    ),

    events = beep_ubus_method(conn,
        function(req, msg)
            -- Send 0 to force a refresh.
            if msg.seq < near_seq then
                -- requestor is too far behind
                local reply = {}
                for _, v in pairs(latest_states) do
                    table.insert(reply, v)
                end
                local response = format_response(reply, true)
                -- log:debug("Sending response #%s/%s: %s", msg.seq, near_seq, response)
                beep_reply(conn, req, beep_success(response))
            elseif msg.seq >= (cur_seq - 1) then
                -- we are up to date
                local now = os.time()
                table.insert(parked_requests,{stamp=now,request=req})
            else
                -- requestor's state is in range of most recent cache
                local reply = {}
                local start_index = msg['seq'] + 1
                for c=start_index, cur_seq - 1 do
                    table.insert(reply, recent_events[c])
                end
                beep_reply(conn, req, beep_success(format_response(reply, false)))
            end
        end, {seq = ubus.INT32}),

    _get_state = beep_ubus_method(conn,
        function(req, msg) -- for debugging only, returns tables to log:debug
            local reply = {}
            reply['sequence'] = cur_seq
            reply['near_sequence'] = near_seq
            reply['recent_events'] = {}
            for k,v in pairs(recent_events) do
                -- log:info('k = %s', k or 'nil')
                -- log:info('v = %s', v or 'nil') 
                reply['recent_events'][tostring(k)] = v
            end
            reply['latest_states'] = latest_states
            reply['parked_requests'] = #parked_requests
            beep_reply(conn, req, beep_success(reply))
        end, {__unused = ubus.STRING})
}

-- ### agent start ###
conn:add(objects)

remote_manager = beephead_remotes.new(conn, on_remote_event)

uloop.run()

