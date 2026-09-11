module(..., package.seeall)

-- Contains the mode: starting, audio, wifisetup, update
-- Contains distributor state for the current group, wifisetup state, and
-- beepupdate state

require 'ubus'
require 'util'

require 'beep_ubus'
local log = require 'log'

require 'beepio_clock'
require 'beepio_bus'
require 'beepio_wifisetup'

local netcheck = require 'beepmanager_netcheck'

-----
-- The functions below are old, taken from the previous implementation of
-- beepio (beepdevio2). They rely on beephead to fetch the state of another
-- system. Since they already work well and are battle-tested I left them
-- pretty much alone and connected them to the SystemModel class through
-- callbacks.
-----

local beepmanager_removed_handler
local context_change_handler
local master_state_handler

local STARTUP_STATE_ESTABLISH_CONTEXT = 1
local STARTUP_STATE_GET_GROUP_CONFIG = 2
local STARTUP_STATE_GET_INITIAL_STATE = 3
local STARTUP_STATE_STARTED = 4

local startup_state = STARTUP_STATE_ESTABLISH_CONTEXT

context = nil
local_id = nil

ubus_conn = nil

function parse_event(result)
    update_pending = false

    if not result then
        return
    end

    local data = result['data']
    local context_state = data[context]
    if not context_state then
        return
    end

    local audio_state = nil
    for i, v in ipairs(context_state) do
        if v['object'] == 'audio' then
            audio_state = v['state']
            break
        end
    end

    master_state_handler(audio_state)
end

function update_handler(event, msg)
    if update_pending then
        return
    end

    update_pending = true
    ubus_call(ubus_conn, 'beep.head', 'events', {seq =  0}, parse_event)
end

function get_group_config_done(result)
    if startup_state ~= STARTUP_STATE_GET_GROUP_CONFIG then
        establish_context()
    end
    if not result then
        --log:debug('Beep.manager not ready, retrying...')
        uloop.timer(establish_context, 1000)
        return
    end

    context = 'group.' .. result['sink_id']

    if not context then
        log:error('establish_context failed: No master found with ' ..
            'source_id == ' .. sink_id)
        os.exit(-1)
    end

    local_id = result['device_id']
    log:debug('Local ID set to ' .. local_id)

    context_change_handler(context)

    startup_state = STARTUP_STATE_GET_INITIAL_STATE

    ubus_call(ubus_conn, 'beep.head', 'events', {seq =  0}, function()
        if startup_state ~= STARTUP_STATE_GET_GROUP_CONFIG then
            establish_context()
        end

        startup_state = STARTUP_STATE_STARTED
        parse_event()
    end)
end

function establish_context()
    startup_state = STARTUP_STATE_GET_GROUP_CONFIG
    ubus_call(ubus_conn, 'beep.manager', 'get_group_config', nil, function(result)
        if not result then
            --log:debug('Beep.manager not ready, retrying...')
            uloop.timer(establish_context, 1000)
            return
        end

        context = 'group.' .. result['sink_id']

        if not context then
            log:error('establish_context failed: No master found with ' ..
                'source_id == ' .. sink_id)
            os.exit(-1)
        end

        local_id = result['device_id']
        log:debug('Local ID set to ' .. local_id)

        context_change_handler(context)

        ubus_call(ubus_conn, 'beep.head', 'events', {seq =  0}, parse_event)
    end, {timeout_millis = 1000, timeout_ok=true, not_found_ok=true})
end

local function try_to_update(on_done)
    log:info('Running beepupdate to check for an update!')

    -- Use start-stop-daemon to disown beepupdate.
    uloop.process('/sbin/start-stop-daemon',
            {'-S', '-b', '-x', '/beep/update_then_beepmanager'},
            nil, nil,
            function(return_code)
                if on_done then
                    on_done()
                end
            end)
end

-----
-- Public interface
-----

MODE_STARTING = 'starting'
MODE_AUDIO = 'audio'
MODE_WIFISETUP = 'wifisetup'
MODE_UPDATE = 'update'

SYSTEM_MODES = {MODE_STARTING, MODE_AUDIO, MODE_WIFISETUP, MODE_UPDATE}

MASTER_STATE_STARTING = 1
MASTER_STATE_WAITING_FOR_STATE = 2
MASTER_STATE_READY= 3

class('SystemModel')
function SystemModel:_init()
end

function SystemModel:init()
    -- need to declare these after ubus_conn has been defined.
    local ubus_objects = {}
    ubus_objects['beep.io'] = {
        __stop = beep_ubus_method(ubus_conn,
            function(req, msg)
                log:debug('*STOP*')
                beep_reply(ubus_conn, req, beep_success())
                uloop.cancel()
            end,  {__unused = ubus.STRING}
        ),
        __set_context = beep_ubus_method(ubus_conn,
            function(req, msg)
                log:debug('Set context to: ' .. msg['context'])
                context = msg['context']

                if startup_state == STARTUP_STATE_GET_GROUP_CONFIG
                        or startup_state == STARTUP_STATE_GET_INITIAL_STATE then
                    context_change_handler(context)
                    update_handler(nil, nil)
                else
                    -- got a __set_context while starting, try again.
                    -- TODO this is still racy, we may get __set_context after
                    -- get_group_config succeeds, but for the previous context.
                    startup_state = STARTUP_STATE_ESTABLISH_CONTEXT
                    establish_context()
                end

                beep_reply(ubus_conn, req, beep_success())
            end, {context = ubus.STRING}
        )
    }

    beepio_wifisetup.add_ubus_object(ubus_objects, ubus_conn)

    ubus_conn:add(ubus_objects)
    ubus_conn:listen('beep.update', update_handler)
    ubus_conn:listen('ubus.object.remove', function(event, msg)
        if msg.path == 'beep.manager' then
            log:info('Beep manager removed, waiting for it to come back.')
            beepmanager_removed_handler()
            startup_state = STARTUP_STATE_ESTABLISH_CONTEXT
            if not (startup_state == STARTUP_STATE_GET_GROUP_CONFIG
                    or startup_state == STARTUP_STATE_GET_INITIAL_STATE) then
                establish_context()
            end
        end
    end)

    uloop.timer(establish_context, 1)

    self._listeners = {}

    self._vals = {}
    self._vals['mode'] = {v=MODE_STARTING, t=0}

    self._master_state_progress = MASTER_STATE_STARTING

    context_change_handler = function(context)
        self._context = context
        self:_handle_context_change()
    end
    master_state_handler = function(audio_vals)
        self:_master_state_handler(audio_vals)
    end
    beepmanager_removed_handler = function()
        if self:get('mode') == MODE_AUDIO then
            self._vals['mode'] = {v=MODE_STARTING, t=beepio_clock.millis()}
            beepio_bus.send_event('system.mode_change')
        end
    end

    beepio_bus.add_handler('wifisetup', 1, function(time, is_on)
        if is_on then
            self._vals['mode'] = {v=MODE_WIFISETUP, t=beepio_clock.millis()}
            beepio_bus.send_event('system.mode_change')
        else
            self._vals['mode'] = {v=MODE_STARTING, t=beepio_clock.millis()}
            beepio_bus.send_event('system.mode_change')
            establish_context()
        end
    end)

    if beepio_wifisetup.setup_is_on() then
        -- send on a timer because handlers may not be registered at this point
        uloop.timer(function()
            self._vals['mode'] = {v=MODE_WIFISETUP, t=beepio_clock.millis()}
            beepio_bus.send_event('system.mode_change')
        end, 1)
    end

    log:info("wifisetup on boot:  %s, setup_is_on: %s",
            beepio_wifisetup.should_enter_setup_on_boot(),
            beepio_wifisetup.setup_is_on())
    if beepio_wifisetup.should_enter_setup_on_boot()
            and not beepio_wifisetup.setup_is_on() then
        log:info("ENTERING WIFISETUP")
        uloop.timer(function()
            beepio_wifisetup.enable_setup_mode()
        end, 1)
    else
        log:info("Waiting for network to connect...")
        uloop.timer(function()
            netcheck.wait_til_up(function()
                netcheck.wait_til_year_set(20, try_to_update)
            end)
        end, 1)
    end
end

function SystemModel:get(key)
    local val_info = self._vals[key]
    if val_info then
        return val_info.v
    end
end

function SystemModel:get_change_millis(key)
    local val_info = self._vals[key]
    if val_info then
        return val_info.t
    end
end

function SystemModel:_handle_context_change()
    self._master_state_progress = MASTER_STATE_WAITING_FOR_STATE
    if self:get('mode') == MODE_AUDIO then
        self._vals['mode'] = {v=MODE_STARTING, t=beepio_clock.millis()}
        beepio_bus.send_event('system.mode_change')
    end
end

function SystemModel:_master_state_handler(audio_vals)
    self._master_state_progress = MASTER_STATE_READY

    local updated_audio_state = false
    local new_audio_state = audio_vals['audio_state']
    if self:get('audio_state') ~= new_audio_state then
        self._vals['audio_state'] = {v=new_audio_state, t=beepio_clock.millis()}
        updated_audio_state = true
        -- only send the event after we send the mode change if we have one,
        -- but we want the new audio state avaialable via :get now.
    end

    if self:get('mode') == MODE_STARTING then
        self._vals['mode'] = {v=MODE_AUDIO, t=beepio_clock.millis()}
        beepio_bus.send_event('system.mode_change')
    end

    if updated_audio_state then
        beepio_bus.send_event('system.audio_state_change')
    end

    -- Find local device among players
    local new_volume
    if audio_vals['players'] then
        for k, v in pairs(audio_vals['players']) do
            if k == local_id then
                new_volume = v['volume']
            end
        end
    end

    if new_volume and self:get('local_volume') ~= new_volume then
        self._vals['local_volume'] = {v=new_volume, t=beepio_clock.millis()}
        beepio_bus.send_event('system.volume_change')
    end
end

system = SystemModel()

function init(conn)
    ubus_conn = conn
    system:init()
end
