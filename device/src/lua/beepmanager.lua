#!/usr/bin/env lua

require 'beepmanager_grouper'

require 'ubus'
require 'uloop'
require 'uci'
require 'strict'
require 'util'
require 'math'

require 'beep_ubus'

local AgentManager = require 'agent_manager'
local AppManager = require 'beepmanager_apps'
local netcheck = require 'beepmanager_netcheck'
local networking = require 'beepmanager_networking'

local config = require 'config'
local device = require 'device'
local log = require 'log'
local flags = require 'flags'
local inspect = require 'inspect'
local beep = require 'beep'
local crypto = require 'crypto'

local AGENT_START_TIMEOUT = 20000
local BEEPDISCOVERY_PATH = './beepdiscovery'
local BEEPHEALTH_PATH = '/usr/bin/lua ./lua/beephealth.lua'

local UHTTPD_RPC_PATH = '/synapse'

flags.add('no_manage_distributor', false)
flags.add('no_manage_playnet', false)
flags.add('no_beephead_simple', false)
flags.add('urelay_port', true, DEFAULT_URELAY_PORT)
flags.add('playnet_port', true)
flags.add('uhttpd_port', true, 80)
flags.add('spotify_port', true, 9303) -- TODO: Need to find a better way to
                                      -- isolate virtual spotify's
flags.add('ssdp_port', true, 32334)
flags.add('dial_port', true, 32335)
flags.add('msg_port', true, 32336)

flags.add('vm', false)

flags.init(arg)

if not flags.ubus then
    flags.ubus = '/var/run/ubus.sock'
end

-- unfortunately we have to include this after initializing flags, otherwise
-- config.lua gets the wrong value for --uciconfig, since flags hasn't yet been
-- initialized
local grouping = require 'beepmanager_grouping'
local updates = require 'beepmanager_updates'

log:init('beepmanager')

log:info('BEEPMANAGER STARTING')

local manage_distributor = not flags.flags['no_manage_distributor']
local manage_playnet = not flags.flags['no_manage_playnet']
local urelay_port = tonumber(flags.flags['urelay_port'])

local apps = {}

-- Initialize ubus
uloop.init()
conn = beep_ubus_connect('beepmanager')

-- Initialize agent manager and app manager
AgentManager:init(conn)
AppManager:init(conn)

-- Forward declaration for ubus objects table
local objects = {}

-- Forward declarations for management objects
local agents = {}


function generate_state()
    local _state = {}
    _state.local_device = deepcopy(grouping.local_config)
    _state.local_device.group_label = grouping.get_group_label()

    -- Other devices don't need to see that this is actually a pending sink_id.
    -- They should treat it as if it were already set
    if grouping.local_config.pending_sink_id ~= nil then
        log:info('Replacing state sink id with pending => %s',
                grouping.local_config.pending_sink_id)
        _state.local_device.pending_sink_id = nil
        _state.local_device.sink_id = grouping.local_config.pending_sink_id
    end

    if grouping.local_config.source_id ~= '-1' then
        -- BEEP-201
        -- _state.local_device.set_uuid = generate_uuid_from_ids(get_set_string())
        _state.local_device.set_uuid = generate_uuid_from_ids(
                grouping.local_config.device_id)
    else
        _state.local_device.set_uuid = '00000000-0000-0000-0000-000000000000'
    end

    _state.local_device.apps = apps

    -- Augment with grouping and device info from internal state
    _state.devices = grouping.devices

    local ids = {}
    _state.groups = {}

    -- Only include sources in groups table that have sinks (otherwise they
    -- are invalid)
    -- Construct sink id set
    local sink_id_set = {}
    for dev_id, dev_obj in pairs(grouping.devices) do
        sink_id_set[dev_obj.sink_id] = true
    end

    -- Collect source id's and add device ids IFF the source_id exists in the
    -- sink id set
    for dev_id, dev_obj in pairs(grouping.devices) do
        if dev_obj.source_id ~= '-1' and sink_id_set[dev_obj.source_id] then
                _state.groups[dev_obj.source_id] = dev_id
        end
    end

    return _state
end

function trigger_update(event_name, event_data)
    beep_send_state(conn, 'manager', event_name, event_data, generate_state())
end


-- Makes the ubus call to distributor to remove a player
-- This can be called on two occassions: when a device is reported dead
-- by beepdiscovery, and when a device leaves the group
function remove_device_from_distributor(id)
    ubus_call(conn, 'beep.distributor', 'remove_device',
            {id = id},
            function(result)
                log:info('Device removed from distributor: ' .. id)
            end)
end

function on_grouping_changed(new_source_id, joining_sink_id, leaving_sink_id,
        integrations)
    -- This is a multipurpose callback called with one or more of the defined
    -- parameters.  Each block below independently checks each parameter and
    -- performs required operations.  In practice, this callback is only
    -- ever called with one non-nil parameter, but should be callable with
    -- more than one.

    -- Check integrations
    if integrations ~= nil then
        if AppManager:sync_integrations(integrations) then
            trigger_update('grouping_changed')
        end
    end

    -- We are becoming a source, need to add players to our distributor
    if new_source_id ~= nil then
        if new_source_id ~= '-1' then
            grouping.local_config.source_time = os.time()

            -- Make sure this is set before triggering the update, or else we
            -- generate an invalid grouping_changed
            trigger_update('grouping_changed')
            local device_list = {}
            for dev_id,dev_obj in pairs(grouping.devices) do
                if dev_obj.sink_id == new_source_id then
                    table.insert(device_list, grouping.devices[dev_id])
                end
            end

            local function add_player()
                local dev = table.remove(device_list,1)
                if not dev then
                    log:info('Players added; starting beepcomm')
                    ubus_call(conn, 'beep.comm.control', 'start', {},
                        function(result)
                            log:info('beepcomm started; starting apps...')
                            AppManager:start_all()
                        end)
                    return
                end
                ubus_call(conn, 'beep.distributor', 'add_player',
                        {ip = dev.ip, port = dev.port, id = dev.id,
                        name = dev.name},
                    function(result)
                        log:info('Device added: ' .. dev.id)
                        add_player()
                    end)
            end
            add_player()
        else -- We are no longer source, clear players on distributor
            ubus_call(conn, 'beep.distributor', 'remove_all_devices', {},
                    function(result)
                        log:info('All devices removed from distributor')
                        ubus_call(conn, 'beep.comm.control', 'stop', {},
                            function(result)
                                log:info('beepcomm stopped; stopping apps...')
                                AppManager:stop_all()
                            end)
                        trigger_update('grouping_changed')
                    end)
        end
    end

    if joining_sink_id ~= nil then
        local dev = grouping.devices[joining_sink_id]
        if dev then
            ubus_call(conn, 'beep.distributor', 'add_player',
                    {ip = dev.ip, port = dev.port,
                     id = joining_sink_id, name = dev.name},
                    function(result)
                        log:info('Device added to distributor: ' .. joining_sink_id)
                    end)
        else
            log:error('Got joining_sink_id but don\'t have dev')
        end
    end

    if leaving_sink_id ~= nil then
        remove_device_from_distributor(leaving_sink_id)
    end
end

-- Persistent group id logic
-- When on_sink_id_changed is called back, we start or reset a 5 second
-- timer that writes the current sink_id to UCI.
local group_id_persist_timer = nil

function on_group_id_persist()
    if group_id_persist_timer then
        group_id_persist_timer:cancel()
    end
    group_id_persist_timer = nil

    config.data_set('group_id', grouping.local_config.sink_id)
    log:info('Group ID saved')

end

function reset_group_id_persist_timeout()
    if group_id_persist_timer then
        group_id_persist_timer:cancel()
    end
    group_id_persist_timer = uloop.timer(on_group_id_persist, 5000)
end

function on_sink_id_changed(new_sink_id)
    -- Not safe to reach into grouping.local_config here for sink_id, because
    -- it may still be a pending change.  Use the new_sink_id argument instead
    trigger_update('grouping_changed')

    ubus_call(conn, 'beep.io', '__set_context',
        {context = 'group.' .. new_sink_id}, function(result)
            log:info('beep.io context set to group.'
                    .. new_sink_id)
        end)

    reset_group_id_persist_timeout()
end

function on_device_updated()
    trigger_update('device_updated')
end

function on_device_ready(device)
    grouping.device_ready(device)
end

function on_device_removed(dev_id)
    grouping.device_removed(dev_id)

    -- Remove from distributor if necessary (idem.)
    remove_device_from_distributor(dev_id)

    -- This notifies apps and beephead that the device is gone and labels
    -- have changed.
    trigger_update('device_removed')
end


------------------------
-- App starting logic --
------------------------

level1_agents = {
    beepdiscovery = {
        manage = true,
        exec_path = string.format(
                '%s --ubus=%s --control_port=%d --uhttpd_port=%d '
                        .. '--uciconfig=%s %s',
                BEEPDISCOVERY_PATH, flags.flags['ubus'],
                flags.flags['urelay_port'], flags.flags['uhttpd_port'],
                flags.flags['uciconfig'],
                flags.flags['vm'] and '--virtual' or ''),
        started = false,
        should_run = true
    },

    beephealth = {
        manage = true,
        exec_path = string.format(
                '%s --campfire --ubus=%s --uciconfig=%s %s',
                BEEPHEALTH_PATH,
                flags.flags['ubus'],
                flags.flags['uciconfig'],
                flags.flags['vm'] and '--vm' or ''),
        started = false,
        should_run = true
    }

    --beephead_simple = {
    --    manage = true,
    --    exec_path = './beephead_simple ' ..
    --        ' --ubus=' .. flags.flags['ubus'],
    --    should_run = not flags.flags['no_beephead_simple']
    --}
}

function on_level0_agents_started()
    -- Only start uhttpd after beephead so that http requests can't be
    -- made until beephead is ready.
    --
    -- But we need to make sure uhttpd is ready before beepdiscovery starts
    -- advertising.
    --
    -- Hack: uhttpd has no ubus object so we can't wait for it to appear.
    -- We do a sleep after starting it instead.
    --
    -- NOTE: start_agents currently has no way to not include an agent
    --     in the count of agents to wait for. If it's managed we wait for
    --     it. So don't include uhttpd in a list of other agents that you
    --     want to wait for.
    start_agents({
        uhttpd = {
            manage = true,
            exec_path = string.format(
            'uhttpd -f -p %s -u %s -U %s -a -D -h static',
            flags.flags['uhttpd_port'], UHTTPD_RPC_PATH,
            flags.flags['ubus']),
            should_run = true
        }
    })
    --os.execute('sleep 1')

    -- Begin listening for other beep devices
    networking.init(conn, on_device_ready, on_device_removed)

    updates.init()

    start_agents(level1_agents)

    log:info('Started.')
end

playnet_exec_path = './playnet' ..
        ' --ubus=' .. flags.flags['ubus'] ..
        ' --uciconfig=' .. flags.flags['uciconfig']

if flags.flags['playnet_port'] then
    playnet_exec_path = playnet_exec_path ..
        ' --port_base=' .. flags.flags['playnet_port']
end

level0_agents = {
    distributor = {
        manage = not flags.flags['no_manage_distributor'],
        exec_path = '/usr/bin/lua lua/distributor.lua '
            .. ' --ubus=' .. flags.flags['ubus']
            .. ' --uciconfig=' .. flags.flags['uciconfig'],
        ubus_object_name = 'beep.distributor',
        should_run = true
    },

    playnet = {
        manage = not flags.flags['no_manage_playnet'],
        ubus_object_name = 'beep.playnet',
        exec_path = playnet_exec_path,
        should_run = true
    },

    urelay = {
        manage = true,
        exec_path = './urelay ' ..
            ' --ubus=' .. flags.flags['ubus'] ..
            ' --urelay_port=' .. flags.flags['urelay_port'],
        ubus_object_name = 'urelay',
        should_run = true
    },

    beephead = {
        manage = true,
        exec_path = '/usr/bin/lua lua/beephead.lua ' ..
            ' --ubus=' .. flags.flags['ubus'] ..
            ' --publish_ubus_events',
        ubus_object_name = 'beep.head',
        should_run = true
    },

    beepcomm = {
        manage = true,
        exec_path = './beepcomm ' ..
        ' --ubus=' .. flags.flags['ubus'] ..
        ' --uciconfig=' .. flags.flags['uciconfig'] ..
        ' --ssdp_port=' .. flags.flags['ssdp_port'] ..
        ' --dial_port=' .. flags.flags['dial_port'] ..
        ' --msg_port=' .. flags.flags['msg_port'],
        ubus_object_name = 'beep.comm.control',
        should_run = true
    },

    beepcloud = {
        manage = true,
        exec_path = './beepcloud ' ..
        ' --ubus=' .. flags.flags['ubus'] ..
        ' --uciconfig=' .. flags.flags['uciconfig'],
        ubus_object_name = 'beep.cloud',
        should_run = true
    },
}

function start_agents(agents, on_all_started)
    local num_to_start = 0
    local started_count = 0
    for agent_name, v in pairs(agents) do
        if v.should_run and v.manage then
            num_to_start = num_to_start + 1
            AgentManager:start_agent(
                v.manage and v.exec_path,
                v.env,
                v.ubus_object_name,
                AGENT_START_TIMEOUT,
                function(pid)  -- on started
                    log:info('Agent started: ' .. agent_name)
                    started_count = started_count + 1
                    if started_count == num_to_start and on_all_started then
                        on_all_started()
                    end
                end,
                function()  -- on timeout
                    log:error('Agent start timeout: '
                            .. agent_name .. '. Exiting...')
                    os.exit(1)
                end,
                function()  -- on removed
                    log:error('Agent died: ' .. agent_name .. '. Exiting...')
                    os.exit(1)
                end
            )
        end
    end
end

objects = {}
objects['beep.manager'] = {
    set_group = beep_ubus_method(conn,
        function(req, msg)
            local group_id
            if msg['group_id'] == nil then
                group_id = new_uuid()
            else
                group_id = tostring(msg['group_id'])
            end

            log:info('*** SET GROUP %s ==> %s ***', msg.device_id, group_id)

            if msg.device_id ~= grouping.local_config.device_id then
                local dev = networking.get_device(msg.device_id)
                if dev == nil then
                    beep_reply(conn, req, beep_error('Device ' ..
                        msg.device_id .. ' not found'))
                    return
                end

                dev:call('beep.manager', 'set_group', msg,
                    function(result, errors)
                        if result then
                            beep_reply(conn, req, beep_success(result))
                        else
                            beep_reply(conn, req, beep_error(
                                        errors.beep_error_message,
                                        errors.beep_error_code))
                        end

                    end)
                return
            end

            if grouping.local_config.sink_id == group_id then
                log:info('set_group called with current group_id')
                beep_reply(conn, req, beep_success())
                return
            end

            if group_id == '-1' or group_id == nil then
                log:warn('set_group called with missing or invalid id')
                beep_reply(conn, req, beep_error())
                return
            end

            grouping.set_group_id(group_id)

            beep_reply(conn, req, beep_success({group_id = group_id}))
        end,
        {device_id = ubus.STRING} -- option: group_id = ubus.STRING
    ),

    integration_rpc = beep_ubus_method(conn,
        function(req, msg)
            -- guaranteed to have name and method, params is an optional
            -- parameter with default nil

            -- validate name: must have "app." prefix and at least one
            -- character
            if(#msg['name'] < 5) then
                local error_msg = 'Invalid integration app name ' .. msg['name']
                --log:error(error_msg)
                beep_reply(conn, req, beep_error(error_msg,
                        BEEP_UBUS_ERROR_ARGS))
                return
            end

            local integration_name = string.sub(msg['name'],5)

            log:info('Got int. rpc call for %s::%s(%s)',
                    integration_name, msg['method'], msg['params'] or '(nil)')

            if not AppManager:has_integration(integration_name) then
                local error_msg = 'Unknown or unavailable integration app ' ..
                        msg['name']
                --log:error(error_msg)
                beep_reply(conn, req, beep_error(error_msg,
                        BEEP_UBUS_ERROR_ARGS))
                return
            end

            local rpc_callback = function(result, errors)
                -- pass result through
                if result then
                    beep_reply(conn, req, beep_success(result))
                else
                    beep_reply(conn, req, beep_error(
                            errors.beep_error_message,
                            errors.beep_error_code))
                end
            end

            if AppManager:is_running(integration_name) ~= 0 then
                -- Local device has this integration assigned, assume it's running
                -- TODO: Don't assume it's running
                log:info('%s found.  Routing RPC locally', integration_name)
                ubus_call(conn, 'beep.integration.' .. integration_name,
                        msg['method'], msg['params'], rpc_callback)
            else
                log:info('%s not found.  Routing RPC remotely', integration_name)
                local found_integration = false
                for dev_id, device in pairs(grouping.devices) do
                    if device.integrations ~= nil then
                        for _,name in ipairs(device.integrations) do
                            if name == integration_name then
                                log:info('%s has integration app %s.  Routing...',
                                        dev_id, integration_name)
                                local dev = networking.get_device(dev_id)
                                if dev == nil then
                                    beep_reply(conn, req, beep_error('Device ' ..
                                            dev_id .. ' not found'))
                                    return
                                end

                                dev:call('beep.manager', 'integration_rpc', msg,
                                        rpc_callback)
                                found_integration = true
                                break
                            end
                        end
                    end

                    if found_integration then
                        break
                    end
                end

                if not found_integration then
                    log:warn('Integration %s not found on any devices',
                            integration_name)

                    beep_reply(conn, req, beep_error('Integration app ' ..
                            integration_name .. ' is unavailable'))
                end
            end

            -- TODO: If no device is running integration, integration is not set
            -- up (currently, all integrations are at least assigned somewhere)

            -- No reply here, reply will come from the ubus_call cb
        end,
        {name = ubus.STRING, method = ubus.STRING}
        -- optional: params = ubus.TABLE
    ),

    -- required for all beep ubus objects
    get_state = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success(generate_state()))
        end,
        {__unused = ubus.STRING}
    ),

    get_group_config = {
        function(req, msg)
            conn:reply(req, beep_success(grouping.local_config))
        end, {unused = ubus.INT32}
    },

    start_app = beep_ubus_method(conn,
        function(req, msg)
            if msg['name'] == nil then
                log:warn('Missing required arg: name')
                beep_reply(conn, req, beep_error())
                return
            end
            AppManager:start_app(msg['name'], function(pid)
                if pid < 0 then
                    beep_reply(conn, req, beep_error(
                            'Did not start, or killed'))
                else
                    beep_reply(conn, req, beep_success())
                end
            end)
        end,
        {name = ubus.STRING}
    ),

    stop_app = beep_ubus_method(conn,
        function(req, msg)
            if msg['name'] == nil then
                log:warn('Missing required arg: name')
                beep_reply(conn, req, beep_error())
                return
            end
            AppManager:stop_app(msg['name'])
            beep_reply(conn, req, beep_success())
        end,
        {name = ubus.STRING}
    ),

    app_running = beep_ubus_method(conn,
        function(req, msg)
            if msg['name'] == nil then
                log:warn('Missing required arg: name')
                beep_reply(conn, req, beep_error())
                return
            end
            local running = AppManager:is_running(msg['name'])
            if running == nil then
                beep_reply(conn, req, beep_error('Unknown app ' .. msg['name']))
            else
                beep_reply(conn, req, beep_success(running))
            end
        end,
        {name = ubus.STRING}
    ),

    notify_acquire = beep_ubus_method(conn,
        function(req, msg)
            if msg['name'] == nil then
                log:warn('Missing required arg: name')
                beep_reply(conn, req, beep_error())
                return
            end
            AppManager:stop_all_but(msg['name'])
            beep_reply(conn, req, beep_success())
        end,
        {name = ubus.STRING}
    ),

    __debug = beep_ubus_method(conn,
        function(req, msg)
            log:debug('__debug_apps')
            local f = io.open('/tmp/manager.txt', 'w')
            f:write(inspect(AppManager.debug_apps()))
            beep_reply(conn, req, beep_success())
            f:close()
        end,
        {__unused = ubus.STRING}
    )
}

-- TODO: This should be dynamically updated

-- Expose ubus object
conn:add(objects)

-- look for apps starting and stopping
conn:listen(
    'ubus.object.add',
    function(event, msg)
        if string_starts(msg.path, 'beep.app.') then
            local first_dot_pos = string.find(msg.path, '%.')
            local app_name = string.sub(
                msg.path, first_dot_pos + 1, #msg.path)
            if not array_contains(apps, app_name) then
                log:info('App started: ' .. app_name)
                table.insert(apps, app_name)
                trigger_update('app_added', {app= app_name})
            else
                log:error('Got ubus.object.add for app we already had:'
                        .. msg.path)
            end
        end
    end)

conn:listen(
    'ubus.object.remove',
    function(event, msg)
        if string_starts(msg.path, 'beep.app.') then
            local first_dot_pos = string.find(msg.path, '%.')
            local app_name = string.sub(
                msg.path, first_dot_pos + 1, #msg.path)
            local i = array_index(apps, app_name)
            if i ~= '-1' then
                log:info('App removed: ' .. app_name)
                table.remove(apps, i)
                trigger_update('app_removed', {app= app_name})
            else
                log:error('Got ubus.object.remove for app we don\'t have:'
                        .. app_name)
            end
        end
    end)

function start0()
    log:debug('Starting Beep services')
    -- Start agents only after waiting 100ms. This is to work around a bug
    -- where event listeners do not work immediately after they're added, since
    -- they have to propagate to ubusd. agent_manager waits for ubus.object.added
    -- to make sure agents are alive. Sometimes those ubus.object.added messages
    -- arrive before our listener is active.
    uloop.timer(function()
        start_agents(level0_agents, on_level0_agents_started)
    end, 100)
end

function log_button_state()
    local devmem_output = get_command_output('devmem 0x18040004')
    local devmem_lines = string_split_by(devmem_output, '\n')
    local last_devmem_line = devmem_lines[#devmem_lines]
    if last_devmem_line then
        local fields = string_split(last_devmem_line)
        if #fields >= 1 then
            log:debug('Button state: %s', fields[#fields])
        else
            log:error('Couldn\'t parse last devmem line')
        end
    else
        log:error('Couldn\'t parse devmem output')
    end
end

local network_died_timer = nil
local have_started = false

function on_network_died()
    log:info('Network has been down for too long, restarting Beep '
            .. 'services. Exiting...')
    os.exit(0)
end

function on_network_up()
    log:debug('Got network up notification')

    -- Set up cluster ID
    if not flags.flags['vm'] and not config.devel_get('force_cluster') then
        local sta_name, sta_key = config.get_station_info()
        local message =
                (sta_name or 'unknown') .. ':' ..
                (sta_key or 'unknown') .. ':' ..
                (get_gateway_mac() or '00:00:00:00:00:00')
        crypto.init()
        local digest = crypto.sha1(message)
        crypto.free()
        config.data_set('cluster_id', digest)
        log:info('cluster_id set to %s', digest)
    else
        log:info('VM mode: cluster_id manually set to %s',
                config.data_get('cluster_id'))
    end

    if network_died_timer then
        network_died_timer:cancel()
    end
    network_died_timer = nil

    if not have_started then
        have_started = true

        log_button_state()

        netcheck.wait_til_year_set(20, start0)
    end
end

function on_network_down()
    log:debug('Got network down notification')
    if network_died_timer then
        network_died_timer:cancel()
    end
    network_died_timer = uloop.timer(on_network_died, 90 * 1000)
end

network_died_timer = uloop.timer(on_network_died, 60 * 1000)

uloop.timer(function()
    log:debug('Waiting for network to come up')
    netcheck.register(on_network_down, on_network_up)
end, 1)

-- Initialize this at the top-level, so that when we start handling requests
-- we're guaranteed to have a valid grouping.local_config
grouping.init(flags.flags['vm'], on_grouping_changed, on_sink_id_changed,
        on_device_updated)
uloop.run()
