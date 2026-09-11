local M = {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    DISCONNECTING = 3
}

require 'beep_ubus'

local log = require 'log'

local ENABLE_TRACE = true

local function debug_trace(...)
    if ENABLE_TRACE then
        log:debug(...)
    end
end


local Device = {}

-- A new Device should only be used after ready_cb has completed.
-- ubus: ubus connection
-- owner: string name of owner, will be used to broadcast device errors on ubus
--     (ex: distributor)
-- is_connected: pass in if we know urelay is already connected (everyone except
--     manager uses this).
-- ip: ip of remove device to connect to
-- port: port of remote urelay
-- id: id string of remote device
-- error_handler: called when a ubus error occurs, the user must stop using
--     the device as soon as they receive this callback.
function Device:init(ubus, owner, is_connected, ip, port, id, error_handler)
    self.ubus = ubus
    self.owner = owner
    self.ip = ip
    self.port = port or DEFAULT_URELAY_PORT
    self.id = id
    self.error_handler = error_handler

    self.sub_handles = {}

    self.allow_urelay_control = not is_connected

    if is_connected then
        self.state = M.CONNECTED
    else
        self.state = M.DISCONNECTED
    end

    -- used internally to hold a callback, if set we know we should reconnect
    -- and we'll call the callback when done reconnecting.
    self.reconnect = nil

    self.connected_cb = nil
    self.disconnected_cb = nil

    self.connect_seq = 0
end

function Device:_relay_connect(done_cb)
    debug_trace('Connecting relay ' .. self.id)
    ubus_call(self.ubus, 'urelay', 'connect',
            {ip = self.ip, port = self.port, id = self.id},
            function (result, errors)
                if not result then
                    log:info('urelay connect failed with errors: %s', errors)
                end
                done_cb(result, errors)
            end)
end

function Device:_relay_disconnect(done_cb)
    ubus_call(self.ubus, 'urelay', 'disconnect',
            {id = self.id},
            function (result, errors)
                if not result then
                    log:error('urelay disconnect failed with errors: %s. Exiting...',
                            errors)
                    os.exit(1)
                else
                    done_cb(result)
                end
            end)
end

function Device:connect(ready_cb)
    if not self.allow_urelay_control then
        log:error('urelay control not allowed for this owner. Exiting...')
        os.exit(1)
    end
    if self.state == M.CONNECTING then
        debug_trace('connect while CONNECTING; updating callback (%s)', self.id)
        self.connected_cb = ready_cb
        return
    elseif self.state == M.DISCONNECTING then
        debug_trace('connect while DISCONNECTING; will reconnect (%s)', self.id)
        self.reconnect = ready_cb
        return
    elseif self.state == M.CONNECTED then
        -- TODO: is this the correct behavior, or should we call ready_cb?
        return
    end

    self.state = M.CONNECTING
    self.connect_seq = self.connect_seq + 1
    local this_seq = self.connect_seq

    debug_trace('CONNECTING %s', self.id)
    self.connected_cb = ready_cb

    self:_relay_connect(function(result, errors)
        if result then
            if self.state ~= M.CONNECTING then
                debug_trace('_relay_connect cb but not CONNECTING (%s)', self.state)
                return
            end

            if self.connect_seq ~= this_seq then
                debug_trace('_relay_connect cb but seq is stale for %s', self.id)
                return
            end

            self.state = M.CONNECTED
            debug_trace('CONNECTED %s', self.id)

            if self.connected_cb then
                local cb = self.connected_cb
                self.connected_cb = nil
                if cb then
                    cb(self)
                end
            end

        else
            self:on_error(errors)
            self:disconnect()
        end
    end)
end

function Device:disconnect(done_cb)
    if not self.allow_urelay_control then
        log:error('urelay control not allowed for this owner. Exiting...')
        os.exit(1)
    end
    if self.state == M.CONNECTED or self.state == M.CONNECTING then
        self.state = M.DISCONNECTING
        debug_trace('DISCONNECTING %s', self.id)

        self.disconnected_cb = done_cb

        self:_relay_disconnect(function()
            self.state = M.DISCONNECTED
            debug_trace('DISCONNECTED %s', self.id)

            local cb = self.disconnected_cb
            self.disconnected_cb = nil
            if cb then
                cb(self)
            end

            if self.reconnect then
                debug_trace('Reconnect field set; reconnecting...')
                local ready_cb = self.reconnect
                self.reconnect = nil
                self:connect(ready_cb)
            end
        end)
    elseif self.state == M.DISCONNECTING then
        self.disconnected_cb = done_cb
    end
    -- otherwise it's already disconnect so do nothing.
end

function Device:on_error(errors)
    self.ubus:send_event('beep.device.error', {
        id=self.id,
        ip=self.ip,
        port=self.port,
        received_by=self.owner,
        errors=errors})
    if self.error_handler ~= nil then
        self:error_handler(errors)
    end
end

-- TODO: don't call done_cb if we've already been cleaned up
function Device:call(path, method, args, done_cb)
    --debug_trace(string.format('Calling %s %s %s', self.id, path, method))
    if self.state ~= M.CONNECTED then
        log:error('call but not CONNECTED %s', self.id)
        done_cb(nil)
        return
    end
    local function on_done(result, errors)
        --debug_trace(string.format('Call %s %s %s complete. Success: %s',
        --        self.id, path, method, result and 'yes' or 'no'))
        if not result then
            self:on_error(errors)
        end
        if done_cb then
            done_cb(result, errors)
        end
    end
    if self.ip == 'local' then
        ubus_call(self.ubus, path, method, args, on_done)
    else
        ubus_relay_call(self.ubus, self.id, path, method, args, on_done)
    end
end

function Device:listen(event_type, event_cb)
    debug_trace(string.format('Listening %s %s', self.id, event_type))
    if self.state ~= M.CONNECTED then
        log:error('listen but not CONNECTED %s', self.id)
        return
    end

    local path = string.format(
            'beep.state.%s.%s',
            event_type,
            (self.ip == 'local') and '_local_' or self.id)

    -- cancel the old handler
    if self.sub_handles[event_type] ~= nil then
        self.ubus:cancel_listen(self.sub_handles[event_type])
    end

    self.sub_handles[event_type] = self.ubus:listen(path, event_cb)
end

function Device:unlisten(event_type)
    self.ubus:cancel_listen(self.sub_handles[event_type])
    self.sub_handles[event_type] = nil
end

-- calls event_cb with key, state_result (as in beep_send_state) as args
-- key is <component>.<dev_id>
--
-- NOTE: We have shown on paper that there could be glitches (events
-- received in the wrong order) if ubus can reorder responses. I believe
-- that ubus cannot reorder responses but haven't proven it.
--
-- Args:
--     component: 'manager', not 'beep.manager'
--     event_cb: callback as described at top of comment
function Device:subscribe(component, event_cb)
    debug_trace(string.format('Subscribing %s %s', self.id, component))
    if self.state ~= M.CONNECTED then
        log:error('subscribe but not CONNECTED %s', self.id)
        return
    end

    local key = component .. '.' .. self.id

    local first = true

    local function on_update(key, msg)
        if not self.sub_handles[component] then
            -- This guards against return a value for the initial get_state if we get
            -- unsubscribed before get_state returns.
            return
        end
        if first then
            msg.event_type = nil
            msg.event_data = nil
        end
        first = false
        event_cb(key, msg)
    end

    self:listen(component,
        function(event, msg)
            on_update(key, msg)
        end)

    self:call('beep.' .. component, 'get_state', nil,
        function(result, errors)
            if not result then
                log:warn('get_state failed for: ' .. self.id
                        .. ' ' .. component)
                -- self:call has already sent the error for us.
            elseif first then
                on_update(key, {
                    event_type = nil,
                    event_data = nil,
                    state = result})
            end
        end)
end

function Device:unsubscribe(component)
    debug_trace(string.format('Unsubscribing %s %s', self.id, component))
    if self.state ~= M.CONNECTED then
        log:error('unsubscribe but not CONNECTED %s', self.id)
        return
    end
    self:unlisten(component)
end

function Device:cleanup()
    for sub_key, sub_handle in pairs(self.sub_handles) do
        log:debug(string.format('Unsubscribing %s %s', self.id, sub_key))
        self.sub_handles[sub_key] = nil
        self.ubus:cancel_listen(sub_handle)
    end
end

function M.new(...)
    local o = {}
    setmetatable(o, Device)
    Device.__index = Device
    o:init(...)
    return o
end

return M
