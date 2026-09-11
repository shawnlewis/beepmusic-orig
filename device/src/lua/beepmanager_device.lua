-- TODO:
--     document state machine
--     document guarantees (on_ready on_dead interleaved)
--
-- Notes: we always set retry_connect_delay to 0 when moving into M.READY, and
-- always set _started_trying_connect_time to 0 when sending on_dead.

local M = {
    INIT = 0,
    CONNECTING = 1,
    CONFIRMING = 2,
    CONFIRMING_TRANSIENT = 3,
    READY = 4,
    DEAD = 5
}

require 'beep_ubus'

local device = require 'device'
local log = require 'log'

log:init('bmdevice')

local INTERFACE_EXPIRE_SECS = 60 * 10

local retry_connect_delay = 0


local Device = {}

-- Callbacks:
--   we call on_ready when an interface moves into the READY state
--   we call on_dead when an interface leaves the READY state (not when it moves to
--       DEAD)
--
-- Invariants:
--   - active_interface will only be nil when INIT or DEAD

function Device:init(ubus, id, on_ready, on_dead)
    self.ubus = ubus
    self.id = id
    self.on_ready = on_ready
    self.on_dead = on_dead
    self.state = M.INIT
    self.interfaces = {}
    self.active_interface = nil
end

function Device:add_interface(ip, port, service_info)
    retry_connect_delay = 0
    local interface = self:_get_interface(ip, port)
    if not interface then
        -- add the interface no matter what state we're in if we don't have it
        local interface = device.new(
                self.ubus, 'beepmanager', false, ip, port, self.id,
                bind(self, '_on_interface_error'))

        -- we tack some private fields onto the interface

        -- specific info that beepdiscovery needs to do a discovery ReconfirmRecord
        interface._service_info = service_info

        -- true if we've received a service removal event for this interface
        interface._have_service_removal = false

        -- the time that we started trying to connect this interface (used to
        -- expire interfaces that we've been trying for awhile)
        interface._started_trying_connect_time = 0

        table.insert(self.interfaces, interface)
    else
        log:debug('Already have interface: %s %s %s %s',
                self.id, ip, port, service_info)
        interface._have_service_removal = false
    end
    -- This is a NOOP if we're already connected
    self:_connect_next_interface()
end

function Device:notify_discovery_removal(service_info)
    -- TODO: this check isn't perfect, if the device has switched IPs we may get a
    -- match when we shouldn't
    local interface = self:_get_interface_by_service_info(service_info)
    if not interface then
        log:warn('Received removal but don\'t have interface: %s', service_info)
        return
    end

    interface._have_service_removal = true

    if self.active_interface ~= interface then
        log:info('Received discovery removal for non-active interface, ignoring')
        -- we'll eventually try to connect to confirm this interface when the current
        -- one fails, so we can leave it in our list.
        return
    end
    if self.state == M.READY then
        log:debug('confirming discovery removal')
        self.state = M.CONFIRMING_TRANSIENT
        self:_do_confirm()
    else
        log:debug('discovery removal while in state %s, ignoring', self.state)
    end
end

function Device:notify_device_error(message)
    log:debug('Received beep.device.error: %s', message, self.active_interface)
    if message.id ~= self.id then
        log:error('We received a device error that\'s not for this device: %s',
                self.id)
        return
    elseif not self.active_interface then
        log:info('Got device_error but no active interface: %s', self.id)
    elseif not (message.ip == self.active_interface.ip
            and message.port == self.active_interface.port) then
        log:info('Got error on non-active interface, discarding: %s %s %s',
                self.id, message.ip, message.port)
        return
    end
    self:_on_interface_error(self.active_interface, message.errors)
end

function Device:matches_service_info(service_info)
    for i, interface in ipairs(self.interfaces) do
        if deep_equals(interface._service_info, service_info) then
            return true
        end
    end
    return false
end

function Device:_get_interface(ip, port)
    for i, interface in ipairs(self.interfaces) do
        if interface.ip == ip and interface.port == port then
            return interface
        end
    end
    return nil
end

function Device:_get_interface_by_service_info(service_info)
    for i, interface in ipairs(self.interfaces) do
        if deep_equals(interface._service_info, service_info) then
            return interface
        end
    end
    return nil
end

function Device:_on_interface_error(interface, errors)
    if not interface then
        log:info('Got error but nil interface')
        return
    end
    if interface ~= self.active_interface then
        log:info('Got error for non-active interface')
        return
    end

    log:debug('Interface error for: %s %s %s ==> %s', self.id, interface.ip,
            interface.port, errors or "nil")

    if self.state == M.CONNECTING then
        self.state = M.DEAD
        self:_on_interface_dead()
    elseif self.state == M.READY then
        self.state = M.CONFIRMING
        self.on_dead(self.active_interface)
        self:_do_confirm()
    elseif self.state == M.CONFIRMING then
        log:info('Confirmation failure, interface is dead')
        self.state = M.DEAD
        self:_on_interface_dead()
    elseif self.state == M.CONFIRMING_TRANSIENT then
        log:info('Transient confirmation failure, interface is dead')
        self.on_dead(self.active_interface)
        self.state = M.DEAD
        self:_on_interface_dead()
    end
end

function Device:_connect_next_interface()
    -- active_interface is non-nil when not in DEAD or INIT
    if not self.active_interface and #self.interfaces ~= 0 then
        -- The active interface is always the first one in the list
        self.active_interface = self.interfaces[1]
        self.state = M.CONNECTING
        if self.active_interface._started_trying_connect_time == 0 then
            self.active_interface._started_trying_connect_time = os.time()
        end
        self.active_interface:connect(bind(self, '_on_interface_connected'))
    end
end

function Device:_on_interface_connected(interface)
    if interface ~= self.active_interface then
        log:warn('Got connect for non-active interface')
        return
    end
    if self.state == M.CONNECTING then
        self.state = M.CONFIRMING
        self:_do_confirm()
    else
        log:warn('Got connect for active interface when not in state CONNECTING')
    end
end

function Device:_should_expire(interface)
    local time = os.time()

    -- we haven't tried to connect this interface since it's last failure.
    if interface._started_trying_connect_time == 0 then
        return false
    end

    -- never expire if we haven't gotten a service removal for this interface
    if not interface._have_service_removal then
        return false
    end

    -- expire if we've been trying for more than X minutes, but never if we
    if time - interface._started_trying_connect_time > INTERFACE_EXPIRE_SECS then
        return true
    end

    return false
end

function Device:_on_interface_dead()
    -- note we don't call self.on_dead here, we've already sent it if the interface
    -- became ready and then went into confirm

    self.active_interface:disconnect()

    -- the active interface is always the first
    if self.active_interface ~= self.interfaces[1] then
        log:error('Programming error, active interface should always be first. '
                .. 'Exiting..')
        os.exit(1)
    end
    table.remove(self.interfaces, 1)
    if not self:_should_expire(self.active_interface) then
        -- put it back on the end of the list.
        table.insert(self.interfaces, self.active_interface)
    else
        log:info('Expiring interface: %s %s %s',
                self.id, self.active_interface.ip, self.active_interface.port)
    end

    self.active_interface = nil
    retry_connect_delay = retry_connect_delay + 1000
    uloop.timer(function()
        if self.state == M.DEAD then
            self:_connect_next_interface()
        end
        -- if we're in another state it means we've had an interface added.
    end, retry_connect_delay)
end

-- Must be called from allowed states
function Device:_do_confirm()
    -- use beep.manager get state to confirm device liveness
    self.active_interface:call('beep.manager', 'get_state', nil,
            bind(self, '_confirm_handler'))
end

function Device:_confirm_handler(result)
    if not result then
        log:debug('confirm handler error')
        -- we don't need to do anything because we'll have been notified
        -- of the error via self._on_interface_error
        return
    end
    if not result.local_device.device_id then
        log:error('Got invalid manager state')
        self:_on_interface_error(self.active_interface, nil)
        return
    end
    if self.id ~= result.local_device.device_id then
        log:info('When confirming we found the wrong device!')
        -- TODO: somehow notify beepmanager so it can try to use this device?
        self:_on_interface_error(self.active_interface, nil)
    end
    if self.state == M.CONFIRMING then
        log:debug('Interface confirmed: %s %s %s',
                self.id, self.active_interface.ip, self.active_interface.port)
        self.state = M.READY
        retry_connect_delay = 0
        self.active_interface._started_trying_connect_time = 0

        -- notify owner
        self.on_ready(self.active_interface)
    elseif self.state == M.CONFIRMING_TRANSIENT then
        log:debug('Removal event was transient, interface is alive: %s %s %s',
                self.id, self.active_interface.ip, self.active_interface.port)
        self.state = M.READY
    else
        log:warn('Got confirm for active interface when not in a confirmation state')
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
