-- beephead_remotes
--
-- This module manages connections to remote beeps and subscriptions to
-- relevant agents on those beeps
--
-- It consumes a BeepManagerSubscriber object and exposes hooks for
-- useful beep events

local log = require 'log'
local device = require 'device'
local beepmanager_subscriber = require 'beepmanager_subscriber'

require 'uloop'

-- ### Private functions ###

local function is_initial_state(msg)
    return msg.event_type == nil
end

-- ### RemoteManager ###

local RemoteManager = {}

function RemoteManager:_init(ubus_context, on_event)
    self.group_interfaces = {}
    self.ubus_context = ubus_context
    self.on_event = on_event or
        function()
            log:warn('Default on_event handler')
        end

    self.beepmanager_sub = beepmanager_subscriber.new(ubus_context,
        function(groups, devices)
            self:_on_grouping_changed(groups, devices)
        end)
end

function RemoteManager:_dispatch(group_id, object, event_type, event_data, state)
    if not self.group_interfaces[group_id] then
        -- we could have removed a group but still have some events in flight for it
        -- although currently we only have subscriptions, which get canceled
        -- properly and can't be called after a cleanup
        return
    end
    self.on_event(group_id, object, event_type, event_data, state)
end

function RemoteManager:_remove_group_interface(group_id)
    local group_interface = self.group_interfaces[group_id]
    if group_interface then
        group_interface:cleanup()

        -- dispatch before actually removing the interface, or else the event
        -- won't get sent
        self:_dispatch( group_id, 'audio', 'group_removed', {}, {})

        self.group_interfaces[group_id] = nil
    end
end

function RemoteManager:_on_grouping_changed(groups, devices)
    log:debug('on_grouping_changed received %s', groups)

    -- unsubscribe from all groups not in groups, or groups with different
    -- source_device_id
    for group_id, group_interface in pairs(self.group_interfaces) do
        if (not groups[group_id]) or
                (groups[group_id] ~= group_interface.id) then
            log:debug('beephead removing group_interface: %s', group_id)
            self:_remove_group_interface(group_id)
        end
    end

    -- Connect to source devices
    for group_id, source_device_id in pairs(groups) do
        if not self.group_interfaces[group_id] then
            local dev_info = devices[source_device_id]
            log:debug('beephead creating group_interface: %s', group_id)
            self.group_interfaces[group_id] =
                    device.new(self.ubus_context,
                            'beephead', true,
                            dev_info.ip,
                            dev_info.port,
                            source_device_id,
                            function(dev, errors)  -- error_cb
                                -- Need to make sure this error is for the interface
                                -- that we have for this group_id, otherwise we'd
                                -- potentially remove a newer interface for the same
                                -- group.
                                if dev == self.group_interfaces[group_id] then
                                    log:debug('beephead removing '
                                            .. 'group_interface due to error: %s',
                                            group_id)
                                    self:_remove_group_interface(group_id)
                                end
                            end)
            self:_create_subscriptions(group_id)
        end
    end
end

function RemoteManager:_create_subscriptions(group_id)
    local interface = self.group_interfaces[group_id]
    if not interface then
        log:warn('interface destroyed before create_subscriptions could be called')
        return
    end

    local function subscribe_to_app(app)
        interface:subscribe(app,
            function(key, msg)
                self:_dispatch(
                    group_id,
                    app,
                    msg.event_type or 'app_added',
                    msg.event_data or {},
                    msg.state)
            end)
    end

    local function unsubscribe_from_app(app)
        interface:unsubscribe(app)
    end

    local function subscribe_to_manager()
        interface:subscribe('manager',
            function(key, msg)
                local event_type = msg.event_type
                if is_initial_state(msg) then
                    for i, app in ipairs(msg.state.local_device.apps) do
                        subscribe_to_app(app)
                    end
                elseif event_type == 'app_added' then
                    local app = msg.event_data.app
                    subscribe_to_app(app)
                elseif event_type == 'app_removed' then
                    local app = msg.event_data.app
                    unsubscribe_from_app(app)
                    self:_dispatch(group_id, app, event_type, {}, {})
                end
            end)
    end

    interface:subscribe('distributor',
        function (key, msg)
            local event_type = msg.event_type
            local event_data = msg.event_data

            if is_initial_state(msg) then
                self:_dispatch(group_id, 'audio', 'group_added', {}, msg.state)
                subscribe_to_manager()
            else
                self:_dispatch(group_id, 'audio', event_type, event_data, msg.state)
            end
        end)
end

function RemoteManager:get_group_interface(group_id)
    return self.group_interfaces[group_id]
end

-- ### Module interface ###

local M = {}

function M.new(...)
    local obj = {}
    setmetatable(obj, RemoteManager)
    RemoteManager.__index = RemoteManager
    obj:_init(...)
    return obj
end

return M
