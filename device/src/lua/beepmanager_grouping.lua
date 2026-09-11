-- This module handles all grouping related tasks
-- It listens for the following
--     device_ready and device_removed callbacks from beepmanager_networking
--         (passed in by beepmanager)
--     remote beepmanager grouping events
--     ubus set_group_id calls
--
-- Interface: This module will call a grouping_changed callback when grouping
--     changes.


local config = require 'config'
local inspect = require 'inspect'
local log = require 'log'
local nl = require 'beepnl'

local beepmanager_updates = require 'beepmanager_updates'

require 'uloop'
require 'util'

log:init('grouping')

-- This is the interface back into the user of this module
local grouping_changed_cb
local sink_id_changed_cb
local device_updated_cb

-- State of each device that we know about. These aren't Device objects, they are
-- tables that contain device information needed for grouping like sink_id, source_id
-- and signal level
local devices = {}

-- The local device config
local local_config = {}
local_config.name = config.data_get('device_name') or 'unknown'
local_config.device_id = config.device_get('device_id') or 'unknown'
if (local_config.device_id:find('%.')) then
    log:error('device_id may not contain a \'.\'')
    os.exit(1)
end
local_config.source_id = '-1'
local_config.source_time = 0
local_config.sink_id = config.data_get('group_id') or '1'
local_config.version = beepmanager_updates.current_version()
log:info('Running version: %s', local_config.version)

-- uloop timer object
local process_group_state_timer = nil
local received_event_during_group_delay = false

-- group_state processing delay constants:
-- Upon receiving a group_state event from a remote device,
-- manager will launch the processor method in GROUP_STATE_TIMER_BASE ms
-- unless another group_state event is received.  In which case, the
-- timer is reset to run in  ( previous delay * GROUP_STATE_TIMER_MULT ) ms
local GROUP_STATE_TIMER_BASE = .5 * 1000
local GROUP_STATE_TIMER_MULT = 1
local group_state_timer_delay = GROUP_STATE_TIMER_BASE

function get_group_label()
    if local_config.source_id == '-1' then
        return ''
    end

    local in_group_dev_names = {}
    for dev_id,dev_obj in pairs(devices) do
        if dev_obj['sink_id'] == local_config.source_id then
            table.insert(in_group_dev_names, dev_obj['name'])
        end
    end

    table.sort(in_group_dev_names)

    return table.concat(in_group_dev_names, ', ')
end

-- Sets source id of this device.  Distributor is notified as appropriate
function set_source_id(source_id)
    if(local_config.source_id == source_id) then
        log:info('set_source_id called with current source_id')
        return
    end

    log:info('Setting source_id to ' .. source_id)

    local_config.source_id = tostring(source_id)
    devices[local_config.device_id].source_id = local_config.source_id

    grouping_changed_cb(local_config.source_id, nil, nil, nil)
end

-- Called after a certain delay upon receiving a group_config
-- event (see annotation above for GROUP_STATE_TIMER_BASE and *_MULT)
function process_group_state()
    if received_event_during_group_delay then
        received_event_during_group_delay = false
        group_state_timer_delay = group_state_timer_delay *
                GROUP_STATE_TIMER_MULT
        log:debug('Resetting timer to %sms', group_state_timer_delay)
        process_group_state_timer = uloop.timer(process_group_state,
                group_state_timer_delay)
        return
    end

    process_group_state_timer = nil

    -- Commit the pending sink id locally
    if local_config.pending_sink_id then
        log:info('Commit pending sink id %s', local_config.pending_sink_id)
        local_config.sink_id = local_config.pending_sink_id
        local_config.pending_sink_id = nil
    end

    local config = {}
    for dev_id, dev_obj in pairs(devices) do
        config[dev_id] = {}
        config[dev_id].sink = dev_obj.sink_id
        config[dev_id].source = dev_obj.source_id
        config[dev_id].signal = dev_obj.sig_avg
        config[dev_id].source_time = dev_obj.source_time
        config[dev_id].version = dev_obj.version
    end

    local new_config, delta
    new_config, delta = assign_groups(config)
    log:debug('new_config: ' .. inspect(new_config))
    log:debug('delta: ' .. inspect(delta))

    -- If this device is in the delta, we must set source id to something
    -- else.
    for dev_id, params in pairs(delta) do
        if dev_id == local_config.device_id and
                params.source ~= nil then
            set_source_id(params.source)
        end
    end

    if new_config[local_config.device_id] ~= nil then
        local integrations = new_config[local_config.device_id].integrations
        local_config.integrations = integrations
        grouping_changed_cb(nil, nil, nil, integrations)
    end

    beepmanager_updates.do_version_check(local_config, new_config)

    for dev_id, config in pairs(new_config) do
        -- The operations below are idempotent so no need to check with
        -- distributor if they (don't) exist
        -- Incoming device sink is our source, therefore add it!
        if config.sink == local_config.source_id then
            log:info('Adding device %s', dev_id)
            grouping_changed_cb(nil, dev_id, nil)
        else -- Otherwise, remove it!
            log:info('Removing device %s', dev_id)
            grouping_changed_cb(nil, nil, dev_id)
        end
    end
end

-- Schedules process_group_state, above, depending on recent group_state
-- updates
function schedule_group_state_processor()
    -- Setup timer for calling process_group_state
    if process_group_state_timer == nil then
        log:debug('Creating timer')
        received_event_during_group_delay = false
        group_state_timer_delay = GROUP_STATE_TIMER_BASE
        process_group_state_timer = uloop.timer(process_group_state,
                GROUP_STATE_TIMER_BASE)
    else
        received_event_during_group_delay = true
    end
end

-- Callback for group_config changes from remote devices
function group_state_event_handler(msg, dev)
    -- Sane variable names
    local config = msg.state.local_device
    local dev_id = config.device_id

    -- Add device information to our local devices table,
    -- or update existing information
    devices[dev_id] = {}
    devices[dev_id].sink_id = config.sink_id
    devices[dev_id].pending_sink_id = config.pending_sink_id
    devices[dev_id].source_id = config.source_id
    devices[dev_id].sig_avg = config.sig_avg
    devices[dev_id].source_time = config.source_time
    devices[dev_id].name = config.name
    devices[dev_id].version = config.version
    devices[dev_id].integrations = config.integrations
    devices[dev_id].ip = dev.ip
    devices[dev_id].port = dev.port
    devices[dev_id].id = dev_id

    -- beephead relies on this, if you remove it, things will break in mysterious
    -- ways
    device_updated_cb()

    schedule_group_state_processor()
end


local M = {}

M.local_config = local_config
M.devices = devices

function M.init(is_vm, grouping_changed_handler, sink_id_changed_handler,
                device_changed_handler)
    if not is_vm then
        log:info('Fetching signal strength.')
        local sig_avg = 0;
        while true do
            nl.init()
            sig_avg = nl.get_sig_avg()
            if sig_avg < 0 then
                nl.free()
                break
            else
                log:debug('Invalid signal strength.  Retrying in 1s...')
                nl.free()
                os.execute('sleep 1')
            end
        end
        log:info('Signal strength: %d', sig_avg)
        local_config.sig_avg = sig_avg
    else
        log:info('VM mode: not collecting signal strength info')
        local_config.sig_avg = 0
    end

    local_config.cluster_id = config.data_get('cluster_id')

    grouping_changed_cb = grouping_changed_handler
    sink_id_changed_cb = sink_id_changed_handler
    device_updated_cb = device_changed_handler
end

function M.device_ready(device)
    device:subscribe('manager',
        function(key, msg)
            if (not msg.event_type) or
                    (msg.event_type == 'grouping_changed') then -- Initial state
                -- note that we're listening for grouping_changed from ourself
                -- as well
                group_state_event_handler(msg, device)
            end
        end)
end

function M.device_removed(dev_id)
    if not devices[dev_id] then
        return
    end

    -- Clear out device information
    devices[dev_id] = nil

    -- We're going to need to recalculate groups since this might
    -- have been a source
    schedule_group_state_processor()
end

M.get_group_label = get_group_label

function M.set_group_id(group_id)
    -- local_config.sink_id = group_id
    -- We can't set sink_id yet because this may potentially orphan a
    -- source.  Groups are invalid if they have no devices in it, so we
    -- avoid this state by setting a pending sink_id and retain current sink_id
    -- until process_group_state fires
    local_config.pending_sink_id = group_id
    sink_id_changed_cb(local_config.pending_sink_id)
end

return M
