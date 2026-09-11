#!/usr/bin/env lua

require 'ubus'
require 'uloop'

require 'beep_ubus'
require 'beepcloud'
require 'util'

local flags = require 'flags'
local log = require 'log'
local curl = require 'curl'
local json = require 'JSON'

flags.init(arg)
log:init('nest')
uloop.init()

local conn = beep_ubus_connect('nest')

local EMERGENCY_VOLUME = 350

local auth_token = nil
local api_url = nil
local emergency_state = nil
local event_stream = nil
local _from_state = nil

local reconnect_timer = nil
local retry_delay = 2
local n_devices = 0

local saved = {}
-- Here's where the magic happens
-- If new state is emergency and from state is nil or safe, lower all master
-- volumes to 
function update_audio_state(from_state, new_state)
    function do_volume_call(context, volume)
        log:info('Setting %s to %d', context, volume)
        ubus_call(conn, 'beep.head', 'call', {
            context = context,
            object = 'audio',
            method = 'set_master_volume',
            params = {volume=volume}})
    end

    local lower_volume = nil
    if new_state then
        log:info('Lowering volume')
        lower_volume = true
    elseif (not new_state) and from_state then
        log:info('Restoring volume')
        lower_volume = false
    else
        log:info('Nothing to do')
        return
    end

    -- Gather data
    ubus_call(conn, 'beep.head', 'events', {seq=0},
            function(result)
                if result == nil then
                    log:error('Failed to communicate with beephead')
                    return
                end

                -- iterate over groups
                for group_id,group_obj in pairs(result.data) do
                    if group_id ~= '_force_object_hack' then
                    -- iterate over components within group
                        for i,component in ipairs(group_obj) do
                            if component.object == 'audio' then
                                local master_volume =
                                        component.state.master_volume
                                if lower_volume and
                                        master_volume > EMERGENCY_VOLUME then
                                    saved[group_id] = master_volume
                                    do_volume_call(group_id, EMERGENCY_VOLUME)
                                elseif saved[group_id] ~= nil then
                                    local new_volume = saved[group_id]
                                    saved[group_id] = nil
                                    do_volume_call(group_id, new_volume)
                                end
                            end
                        end
                    end
                end
            end)
end

function process_event(event, data)
    if event == 'put' then -- Update devices table
        local nest_devices = json:decode(data)['data']

        if next(nest_devices) == nil then
            log:warn('Empty smoke_co_alarms table')
            n_devices = 0
            return
        end

        -- Get device count
        n_devices = table_length(nest_devices.smoke_co_alarms)

        local found_emergency = false
        for id, obj in pairs(nest_devices.smoke_co_alarms) do
            -- Could also check if ui_color_state == 'red'
            if obj.smoke_alarm_state == 'emergency' or
                    obj.co_alarm_state == 'emergency' or
                    obj.is_manual_test_active then
                log:warn('*** NEST EMERGENCY: %s ***', id)
                found_emergency = true
                break
            end
        end
        if found_emergency ~= emergency_state then
            log:info('Updating cluster audio state')
            _from_state = emergency_state
            emergency_state = found_emergency
            update_audio_state(_from_state, emergency_state)
        end
    end
end

local pending_event = nil
local pending_data = nil
local last_data = nil -- for debug only

function process_data(ptr)
    -- log:info('Incoming data ==> %s', ptr)
    for _,line in ipairs(string_split_by(ptr,'\n')) do
        if string_starts(line, 'event: ') then
            pending_event = string.sub(line, 8)
        elseif string_starts(line, 'data: ') then
            pending_data = string.sub(line, 7)
        end

        if pending_event ~= nil and pending_data ~= nil then
            local event = pending_event
            local data = pending_data

            pending_event = nil
            pending_data = nil
            if(event ~= 'keep-alive') then
                process_event(event, data)
            end
        end
    end
end

function start_subscription()
    if event_stream ~= nil then
        log:info('Closing existing event stream')
        event_stream:close()
        event_stream = nil
    end

    local target_url = nil
    if api_url then
        target_url = api_url
    else
        target_url = 'https://developer-api.nest.com/' ..
                     'devices?auth=' .. auth_token
    end

    function schedule_retry()
        -- Exponential backoff, up to 5 minutes
        retry_delay = retry_delay * 2
        if(retry_delay > 300) then
            retry_delay = 300
        end

        reconnect_timer = uloop.timer(start_subscription, retry_delay * 1000)
        log:info('Scheduled subscription retry')
    end

    event_stream = curl.event_stream(target_url,
            function(ptr, size) -- data_cb
                last_data = ptr -- debug only
                process_data(ptr)
                retry_delay = 1 -- If this callback is called, we have
                                -- successfully connected; reset retry delay
                                -- to minimum amount
                if reconnect_timer then
                    reconnect_timer:cancel()
                    reconnect_timer = nil
                end
                return 0
            end,

            function(code) -- done_cb
                local delete_api_url = function()
                    log:info('Invalid URL -- flushing stored url')
                    beepcloud.delete_global('nest_api_url',
                            function(success)
                                api_url = nil
                            end)
                end

                if (code ~= 200) then
                    -- Abnormal exit

                    if (code == 401) then
                        -- Unauthorized (lost nest token)
                        -- Delete token and api url
                        log:info('Invalid token -- flushing stored token')
                        beepcloud.delete_global('nest_auth_token',
                                function(success)
                                    delete_api_url()
                                end)
                    elseif (code == 0) then -- Connection failed
                        log:info('Stream event connection failed, retrying...')
                        schedule_retry()
                    else -- Other HTTP error
                        log:info('Stream event closed with code => %d', code)
                        delete_api_url()
                    end
                else
                    -- Connection closed under OK conditions
                    log:info('Subscription ended normally.')
                end

                event_stream = nil

                return 0
            end,

            function(url, code) -- redirect_cb
                -- we get this redirect when we call to the base nest API
                -- url, then firebase sends us to some other endpoint.
                -- Because nest counts redirects as calls, persist URL so
                -- we avoid rate limiting
                api_url = url
                beepcloud.set_global('nest_api_url', url, function(success)
                    log:info('API URL saved')
                end)
                return 0
            end)
    if not event_stream then
        log:info('Failed to create event stream')
        schedule_retry()
    end
end

function check_auth_token()

    function get_token()
        log:debug('Check for auth token')
        beepcloud.get_global('nest_auth_token', function(val)
            if val ~= nil then
                auth_token = val
                log:info('auth token => %s', auth_token)
                start_subscription()
            else
                log:warn('no auth token found')
            end
        end)
    end

    -- Check api url first
    log:debug('Check for api url')
    beepcloud.get_global('nest_api_url', function(val)
        if val ~= nil then
            log:info('Found api url, starting subscription')
            api_url = val
            start_subscription()
        else
            log:warn('no api url found, trying for auth token')
            get_token()
        end
    end)
end

local objects = {}
objects['beep.integration.nest'] = {
    get_state = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success(
                    {
                        active = (event_stream ~= nil),
                        n_devices = n_devices
                    }))
        end, {__unused = ubus.STRING}
    ),

    authenticate = beep_ubus_method(conn,
        function(req, msg)
            -- Delete the api url, then set token, then check auth token
            local _auth_token = msg['token']
            function set_token()
                beepcloud.set_global('nest_auth_token', _auth_token,
                        function(success)
                            check_auth_token()
                            beep_reply(conn, req, beep_success())
                        end)
            end

            beepcloud.delete_global('nest_api_url',
                    function(success)
                        api_url = nil
                        set_token()
                    end)
        end, {token = ubus.STRING}
    ),

    force_check = beep_ubus_method(conn,
        function(req, msg)
            check_auth_token()
            beep_reply(conn, req, beep_success())
        end, {__unused = ubus.STRING}
    ),

    __dump = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn,req, beep_success(
                    {pending_event=pending_event and pending_event or 'nil',
                     pending_data=pending_data and pending_data or 'nil',
                     last_data=last_data and last_data or 'nil',
                     auth_token=auth_token or 'nil',
                     api_url=api_url or 'nil'}))
        end, {__unused = ubus.STRING}
    ),
}

curl.init()
conn:add(objects)

check_auth_token()

uloop.run()
curl.destroy()
