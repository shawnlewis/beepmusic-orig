-- beepmanager_subscriber
--
-- This module provides a means of creating a BeepManagerSubscriber object
-- that subscribes to the local beep manager on a given ubus context and
-- calls appropriate callbacks
--

local log = require 'log'
local device = require 'device'

require 'util'

local function default_on_grouping_changed(groups, devices)
    log:warn('Default (empty) onGroupingChanged handler called!')
end

local function states_are_equal(a, b)
    return (deep_equals(a.devices, b.devices)
            and deep_equals(a.groups, b.groups))
end

local function state_is_valid(state)
    -- All groups' source_device_id's must exist, and those devices must
    -- have source_id = group
    for group_id, source_device_id in pairs(state.groups) do
        if state.devices[source_device_id] == nil then
            log:warn('Group %s points to non-existant device %s',
                    group_id, source_device_id)
            return false
        end

        if state.devices[source_device_id].source_id ~= group_id then
            log:warn('Group %s points to device %s but that device has source_id = %s',
                    group_id, state.devices[source_device_id].source_id)
            return false
        end
    end

    -- All devices must have a sink_id that is in the group table
    for device_id, device_info in pairs(state.devices) do
        if state.groups[device_info.sink_id] == nil then
            log:warn('Device %s has sink_id %s but no such group in groups table',
                    device_id, device_info.sink_id)
            return false
        end
    end

    return true
end

local BeepManagerSubscriber = {
    state = {},
    device = {}
}

function BeepManagerSubscriber:start()
    self.device:subscribe('manager',
        function(key, msg)
            self:_check_new_state(msg.state)
        end)
end

function BeepManagerSubscriber:_init(ubus_context, on_grouping_changed)
    self.on_grouping_changed =
            on_grouping_changed or default_on_grouping_changed

    self.device = device.new(ubus_context, 'beepmanager_subscriber', true,
            'local', nil, '___local___',
            function()  -- on_error
                log:error('beepmanager_subscriber lost connection to local device. '
                        .. 'Exiting...')
                os.exit(1)
            end)
    self:start()
end

function BeepManagerSubscriber:_check_new_state(new_state)
    if state_is_valid(new_state) then
        self.state = new_state
        self.on_grouping_changed(self.state.groups, self.state.devices)
    else
        log:info('Ignoring invalid state change')
    end
end

local M = {} -- Module object

function M.new(...)
    local obj = {}
    setmetatable(obj, BeepManagerSubscriber)
    BeepManagerSubscriber.__index = BeepManagerSubscriber
    obj:_init(...)
    return obj
end

return M
