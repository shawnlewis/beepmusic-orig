-- This module makes all decisions about remote device liveness.
-- It listens for the following
--     beepdiscovery events
--     urelay errors
--     beep.device.error events from other services (distributor etc)
--     netcheck for network liveness
--
-- Interface: this module will called device_ready_cb when a remote device is
-- ready to use, and device_removed_cb when a device is no longer available.


local inspect = require 'inspect'
local log = require 'log'

require 'uloop'
require 'util'

local beepmanager_device = require 'beepmanager_device'

log:init('networking')


local DISCOVERY_EVENT = 'beep.device.add'
local REMOVAL_EVENT = 'beep.device.delete'


-- These are the interface back into the user of this module
local device_ready_cb
local device_removed_cb

local devices = {}


-- Called when a discovered remote device is ready to be queried.
function on_remote_ready(dev)
    log:info('Connected to device: ' .. dev.id)
    device_ready_cb(dev)
end

function on_remote_dead(dev)
    log:info('Remove dead: %s', dev.id)
    device_removed_cb(dev.id)
end


-- Listen for discovery events
-- This connects to remote beep devices and calls get to figure
-- out their info.  If we find a device with the same sink id as our
-- source id, notify the distributor.
function discovery_event_listener(event, msg)
    log:info('Discovered device: %s:%s %s' , msg.ip, msg.port, msg.id)

    if msg.port == 8080 then
        log:info('Ignoring device on 8080')
        return
    end

    if not devices[msg.id] then
        devices[msg.id] =
            beepmanager_device.new(conn, msg.id, on_remote_ready, on_remote_dead)
    end

    devices[msg.id]:add_interface(msg.ip, msg.port, msg.service_info)
end

-- Called when discovery removal events are received
function removal_event_listener(event, msg)
    log:info('Discovery removal event: %s', msg)

    local matches = 0
    for dev_id, device in pairs(devices) do
        if device:matches_service_info(msg.service_info) then
            device:notify_discovery_removal(msg.service_info)
            matches = matches + 1
        end
    end
    if matches == 0 then
        log:info('Didn\'t find device matching discovery removal event')
    elseif matches > 1 then
        log:error('Found more than one device matching discovery removal event. '
               ..  'Programing error. Exiting...')
        os.exit(1)
    end
end

function device_error_listener(event, msg)
    -- Errors on the local device are not network related and this callback
    -- never does anything other than sending a log message about device
    -- not found.
    if msg.id == 'local' then
        return
    end

    local device = devices[msg.id]
    if not device then
        log:error('Received device error for device we don\'t have (%s)',
                msg.id)
        return
    end
    device:notify_device_error(msg)
end


local M = {}

function M.init(ubus_conn, device_ready_handler, device_removed_handler)
    device_ready_cb = device_ready_handler
    device_removed_cb = device_removed_handler

    -- Begin listening for other beep devices
    conn:listen(DISCOVERY_EVENT, discovery_event_listener)

    -- ...and the death of those devices (according to discovery)
    conn:listen(REMOVAL_EVENT, removal_event_listener)

    conn:listen('beep.device.error', device_error_listener)
end

-- returns the active interface for the device
function M.get_device(id)
    if devices[id] then
        return devices[id].active_interface
    end
    return nil
end

return M
