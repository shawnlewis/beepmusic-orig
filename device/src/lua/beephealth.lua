#!/usr/bin/env lua

require 'ubus'
require 'uloop'
require 'uci'
require 'beep'
require 'beep_ubus'
require 'util'

local config = require 'config'
local flags = require 'flags'
local log = require 'log'

log.disable_repeat_suppression()

flags.add('poll_interval',true,'30') -- seconds
flags.add('deadline',true,'5') -- seconds
flags.add('vm',false)
flags.add('verbose',false)
flags.add('campfire',false)
flags.add('quiet',false)

flags.init(arg)

local beep_version = 'UNKNOWN'
local version_file = io.open('VERSION', 'r')
if version_file then
    beep_version = string_strip(version_file:read('*all'))
end

local opkg_path = 'UNKNOWN'
local opkg_conf = io.open('/etc/opkg.conf', 'r')
if opkg_conf then
    while true do
        local line = opkg_conf:read('*line')
        if not line then
            break
        end
        local capture = line:match('^src/gz%s+(.*)')
        if capture then
            opkg_path = capture
            break
        end
    end
end

local POLL_INTERVAL = tonumber(flags.flags['poll_interval']) * 1000
local DEADLINE = tonumber(flags.flags['deadline']) * 1000

local AGENT_BANK = {
    'beepmanager',
    'playnet',
    'urelay',
    'beephead',
    'beepcomm',
    'beepdiscovery',
    'distributor',
}

log:init('beephealth')
uloop.init()

local conn = beep_ubus_connect('beephealth')

local ping_ts_h
local ping_ts_l
local responses = nil

local manager_state
local distributor_state
local head_events
local failed_count = 0
local num_iters = 0

function now()
    return beep.beep_millis()
end

function time_delta(high, low)
    local delta
    _, delta = beep.beep_millis_sub(
        high, low, ping_ts_h, ping_ts_l)
    return delta
end

function on_failure()
    log:error('*** FAILURE, too many failures in a row ***, Exiting...')
    os.exit(1)
end

function log_beep_info()
    log:info('Beep version: %s', beep_version)
    log:info('opkg src: %s', opkg_path)
end

function state_check(manager_state, distributor_state, head_events)
    -- get manager groups
    local groups = {}
    for group_id, master in pairs(manager_state.groups) do
        groups[group_id] = {master = master, devices = {}}
    end
    for device_id, device_info in pairs(manager_state.devices) do
        if not groups[device_info.sink_id] then
            log:warn('Found device %s whose sink_id %s does not match a'
                    .. ' known group',
                    device_id, device_info.sink_id)
            return false
        end
        groups[device_info.sink_id]['devices'][device_id] = 1
    end

    local head_groups = {}
    for key, val in pairs(head_events) do
        if string_starts(key, 'group.') then
            local group_id = key:sub(7)
            if not groups[group_id] then
                log:warn('Head had group %s not in manager groups', group_id)
                return false
            end
            head_groups[group_id] = 1
            for _, event in ipairs(val) do
                if event['object'] == 'audio' then
                    local players = event['state']['players']
                    if not players then
                        log:warn('Head didn\'t have players for group %s',
                                group_id)
                        return false
                    end
                    if not sets_equal(players, groups[group_id]['devices']) then
                        log:warn('Head had wrong players for group %s',
                                group_id)
                        return false
                    end
                end
            end
        end
    end

    if not sets_equal(groups, head_groups) then
        log:warn('Manager had group not in head')
        return false
    end

    local local_source_id = manager_state.local_device.source_id
    if local_source_id ~= '-1' then
        if not groups[local_source_id] then
            log:warn('Manager didn\'t have local device as source')
            return false
        end

        if not sets_equal(groups[local_source_id].devices,
                distributor_state.players) then
            log:warn('Distributor players didn\'t match manager devices')
            return false
        end
    end

    return true
end

function log_periodic_info(manager_state, distributor_state)
    local cluster_id = manager_state.local_device.cluster_id
    local group_id = manager_state.local_device.sink_id
    local is_master = manager_state.local_device.source_id ~= '-1'

    local app = 'nil'
    local audio_state = 'nil'
    if is_master then
        if distributor_state.station.app then
            app = distributor_state.station.app
        end
        audio_state = distributor_state.audio_state
    end

    -- use format so we don't get quotes around strings
    if not cluster_id then
        cluster_id = 'nil'
    end
    if not group_id then
        group_id = 'nil'
    end
    log:info(string.format('STATUS: %s %s %d %s %s', cluster_id, group_id,
            bool_to_num(is_master), app, audio_state))
end

function deadline_cb()
    local failure = false
    local slow = false

    if flags.flags['verbose'] then
        log:info('***Beephealth iteration start***')
    end

    for k,v in pairs(responses) do
        if not v then
            log:error('agent ' .. k .. ': no response ')
            failure = true
        end
    end
    if flags.flags['verbose'] then
        log:info('Ping Responses: %s', responses)

        log_beep_info()

        log:info('***Beephealth iteration end***')
    end

    if not manager_state then
        log:debug('Failed to get manager state!')
    end
    if not distributor_state then
        log:debug('Failed to get distributor state!')
    end
    if not head_events then
        log:debug('Failed to get head events!')
    end

    if manager_state and distributor_state and head_events then
        if not state_check(manager_state, distributor_state, head_events) then
            failure = true
        end
    else
        failure = true
    end

    if failure then
        failed_count = failed_count + 1
        if failed_count >= 4 then
            on_failure()
        end
    else
        failed_count = 0
    end

    num_iters = num_iters + 1
    -- Every 2 minutes
    if num_iters % 4 == 0 then
        log_periodic_info(manager_state, distributor_state)
    end
end

function pinger()
    -- Clear response table
    responses = {}
    for i,v in ipairs(AGENT_BANK) do
        responses[v] = false
    end

    conn:send_event('beep.ping', {})
    ping_ts_h, ping_ts_l = now()
    if flags.flags['verbose'] then
        log:debug('*** PING *** at ' .. ping_ts_h .. ':' .. ping_ts_l)
    end
    schedule_deadline()

    manager_state = nil
    distributor_state = nil
    head_events = nil

    ubus_call(get_ubus_conn(), 'beep.manager', 'get_state', {},
        function(result)
            if result then
                --log:debug('manager result: %s', result)
                manager_state = result
            end
        end)

    ubus_call(get_ubus_conn(), 'beep.distributor', 'get_state', {},
        function(result)
            if result then
                --log:debug('distributor result: %s', result)
                distributor_state = result
            end
        end)

    ubus_call(get_ubus_conn(), 'beep.head', 'events', {seq = 0},
        function(result)
            if result then
                --log:debug('head result: %s', result)
                head_events = result.data
            end
        end)

    uloop.timer(pinger, POLL_INTERVAL)
end

function schedule_pinger()
    uloop.timer(pinger, POLL_INTERVAL)
end

function schedule_deadline()
    uloop.timer(deadline_cb, DEADLINE)
end

objects = {}
objects['beep.health'] = {
    pong = beep_ubus_method(conn,
        function(req, msg)
            local agent = msg['agent']
            if responses[agent] == nil then
                beep_reply(conn, req, beep_success())
                return
            end

            responses[agent] = time_delta(beep.beep_millis())
            beep_reply(conn, req, beep_success())
            if flags.flags['verbose'] then
                log:debug(string.format('%s responded in %dms',
                    agent, responses[agent]))
            end
        end, {agent = ubus.STRING}
    )
}

conn:add(objects)
uloop.timer(pinger, POLL_INTERVAL)
uloop.run()
