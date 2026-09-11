#!/usr/bin/env lua

module(..., package.seeall)

local log = require 'log'
local config = require 'config'
local inspect = require 'inspect'
local flags = require 'flags'
local AgentManager = require 'agent_manager'

local APP_START_TIMEOUT = 40000

local M = {}

log:init('beepmanager_apps')

local apps = {}

STATE_STOPPED = 0
STATE_STARTING = 1
STATE_RUNNING = 2
STATE_STOPPING = 3

function M.debug_apps()
    local result = {}
    for _, app in pairs(apps) do
        result[app.name] = app.state
    end
    return result
end

function M.start_app(self, name, on_finish)
    if not name then
        return
    end

    if not apps[name] then
        log:warn('App %s not found', name)
        if on_finish then
            on_finish(-1)
        end
        return
    end

    apps[name]:start(on_finish)
end

function M.stop_app(self, name, force)
    if not name then
        return
    end

    local app = apps[name]

    if not app then
        log:warn('%s not found', name)
        return
    end

    app:stop(force)
end

-- Integrations are exempt from start_all and stop_all operations
function M.start_all(self, on_finish)
    local n_apps = #apps
    local n_started = 0
    for name, app in pairs(apps) do
        if app.enabled and not app.integration then
            app:start(function(pid)
                if pid < 0 then
                    n_apps = n_apps - 1
                else
                    n_started = n_started + 1
                end

                if n_started >= n_apps and on_finish then
                    log:info('All apps started.')
                    on_finish()
                end
            end)
        end
    end
end

function M.stop_all(self)
    for _, app in pairs(apps) do
        if not app.integration then
            app:stop(true)
        end
    end
end

function M.stop_all_but(self, name)
    for _, app in pairs(apps) do
        if app.name ~= name
                and app.javascript
                and not app.integration then
            app:stop()
        end
    end
end

-- Returns session_id if app is running, 0 if not running, nil on error.
function M.is_running(self, name)
    local app = apps[name]

    if not app then
        log:debug('Got request for unknown app %s', name)
        return nil
    end

    return app:get_session_id()
end

function M.sync_integrations(self, run_list)
    function has_value(tbl, val)
        for _,tblval in ipairs(tbl) do
            if val == tblval then
                return true
            end
        end
        return false
    end

    local state_changed = false

    -- This might seem a little backwards, but we can't
    -- iterate over only run_list because we must stop apps
    -- that are NOT in run_list as well as starting those that are.
    for name,app in pairs(apps) do
        if app.integration then
            if has_value(run_list, name) then
                if self:is_running(name) == 0 then
                    log:info('Starting %s', name)
                    app:start(function()
                        -- Announce started? May occur elsewhere 
                    end)
                    state_changed = true
                end
            else
                if self:is_running(name) ~= 0 then
                    log:info('Stopping %s', name)
                    app:stop(function()
                        -- Announce stopped? May occur elsewhere.
                    end)
                    state_changed = true
                end
            end
        end
    end

    return state_changed
end

function M.get_ordered_integrations(self)
    local ids = {}
    for id,app in pairs(apps) do
        if app.integration then
            table.insert(ids, id)
        end
    end

    table.sort(ids)

    return ids
end

function M.has_integration(self, name)
    return apps[name] ~= nil and apps[name].integration
end

class('App')
function App:_init(name, command, env,
        ubus_object_name, enabled, javascript, zombie, integration)
    self.name = name
    self.env = env
    self.command = command
    self.ubus_object_name = ubus_object_name
    self.javascript = javascript
    self.enabled = enabled
    self.zombie = zombie
    self.integration = integration

    self.restart_delay = 5
    self.state = STATE_STOPPED
    self.session_id = 0
    self.process = nil

    self.on_started_callbacks = {}
    self.on_stopped_callbacks = {}
    self.term_timer = nil
    self.kill_timer = nil

    self.restart_timer = nil
end

function App:start(on_started)
    log:debug('App start: %s', self.name)
    if self.state == STATE_RUNNING then
        log:debug('App is already started')
        if on_started then
            on_started(self.process:get_pid())
        end
    elseif self.state == STATE_STARTING then
        log:debug('App is already starting')
        if on_started then
            table.insert(self.on_started_callbacks, on_started)
        end
    elseif self.state == STATE_STOPPED then
        log:debug('App is stopped, starting...')
        if on_started then
            table.insert(self.on_started_callbacks, on_started)
        end
        self:_do_start()
    elseif self.state == STATE_STOPPING then
        log:debug('App is stopping, deferring start until stopped')
        if on_started then
            table.insert(self.on_started_callbacks, on_started)
        end

        -- wait til the app is all the way stopped before starting it again
        self.restart = true
    end
end

function App:stop(force, on_stopped)
    log:debug('App stop: %s', self.name)
    if self.state == STATE_STOPPED then
        log:debug('App is already started')
        if on_stopped then
            on_stopped()
        end
    elseif self.state == STATE_STOPPING then
        log:debug('App is already stopping')
        if on_stopped then
            table.insert(self.on_stopped_callbacks, on_stopped)
        end
        self.restart = false
        self:_notify_start_waiters(-1)
    elseif self.state == STATE_RUNNING then
        log:debug('App is running, stopping...')
        if on_stopped then
            table.insert(self.on_stopped_callbacks, on_stopped)
        end
        self:_do_stop(force)
    elseif self.state == STATE_STARTING then
        log:debug('App is starting, stopping...')
        if on_stopped then
            table.insert(self.on_stopped_callbacks, on_stopped)
        end
        self:_do_stop(force)
        self:_notify_start_waiters(-1)
    end
end

function App:get_session_id()
    if self.state == STATE_RUNNING then
        return self.session_id
    else
        return 0
    end
end

function App:_do_start()
    if self.state ~= STATE_STOPPED then
        log:error('Programming error: _do_start but state != '
                .. 'STATE_STOPPED: %s. Exiting...', self.name)
        os.exit(1)
    end

    self.state = STATE_STARTING
    self.session_id = self.session_id + 1

    if self.restart_timer then
        self.restart_timer:cancel()
        self.restart_timer = nil
    end

    local instance_session_id = self.session_id
    self.process = AgentManager:start_agent(
        self.command,
        self.env,
        self.ubus_object_name,
        APP_START_TIMEOUT,
        function()
            if self.session_id == instance_session_id then
                self:_on_started()
            else
                -- Can't happen because we try to ensure we're only ever
                -- starting one instance at time.
                log:error('App started for invalid session id: %s. '
                        .. 'Exiting...', self.name)
                os.exit(1)
            end
        end,
        function()
            if self.session_id == instance_session_id then
                self:_on_start_timeout()
            else
                -- Happens when we stop an app during the STARTING state.
                log:debug('Got app start timeout for old session id. '
                        .. 'ignoring. App: %s', self.name)
            end
        end,
        function()
            if self.session_id == instance_session_id then
                self:_on_stopped()
            else
                -- Can't happen because we try to ensure we're only ever
                -- starting one instance at time.
                log:error('Got app stopped for old session id: %s. Exiting...',
                        self.name)
                os.exit(1)
            end
        end)
end

function App:_do_stop(force)
    if not (self.state == STATE_STARTING or self.state == STATE_RUNNING) then
        log:error('Programming error: _do_stop called from wrong state: %s. '
                .. 'Exiting...', self.name)
        os.exit(1)
    end

    if self.zombie and not force then
        log:debug('__ULOOP_CB: %s', __uloop_cb)
        log:info('Merely distracting %s (It cannot be stopped! YIKES)',
                self.name)
        -- we don't set state to STOPPING here, so we'll just try to re-run
        -- it because that's what we do if something dies in RUNNING
    else
        log:info('Stopping %s', self.name)
        self.state = STATE_STOPPING
    end

    local process = self.process
    process:sigterm()
    self.term_timer = uloop.timer(function()
        log:warn('Process did not stop within timeout, sigkilling')
        process:sigkill()
        self.kill_timer = uloop.timer(function()
            log:error('Process did not stop after sigkill, Exiting...')
            os.exit(1)
        end, 2000)
    end, 2000)
end

function App:_on_started()
    if self.state ~= STATE_STARTING then
        log:error('Got app started for app that is not STARTING: %s. '
                .. 'Exiting...', self.name)
        os.exit(1)
    end

    log:info('App started: %s', self.name)
    self.state = STATE_RUNNING
    self.restart_delay = 5

    self:_notify_start_waiters(self.process:get_pid())
end

function App:_on_start_timeout()
    if self.state ~= STATE_STARTING then
        log:error('Got app start timeout for app that is not STARTING: %s. '
                .. 'Exiting...', self.name)
        os.exit(1)
    end

    log:warn('App start timeout: %s, sigkill\'ing', self.name)

    local process = self.process
    process:sigkill()
    self.kill_timer = uloop.timer(function()
        log:error('Process did not stop after sigkill, Exiting...')
        os.exit(1)
    end, 2000)

    self:_notify_start_waiters(-1)
end

function App:_on_stopped()
    ubus_call(conn, 'beep.comm', 'msg_socket_close_all',
            {app_id = self.name}, function(result)
                log:info('beepcomm notified of %s death', self.name)
            end, nil)
    if self.term_timer then
        self.term_timer:cancel()
    end
    if self.kill_timer then
        self.kill_timer:cancel()
    end
    self.term_timer = nil
    self.kill_timer = nil

    if self.state == STATE_RUNNING or self.state == STATE_STARTING then
        -- Died unexpectedly or restart
        self.state = STATE_STOPPED
        log:info('%s died. Restarting in %ss...', self.name, self.restart_delay)
        self.restart_timer = uloop.timer(function()
            log:debug('Starting app after restart delay: %s', self.name)
            self:start()
        end, self.restart_delay * 1000)
        self.restart_delay = self.restart_delay * 2
        if self.restart_delay > 600 then
            self.restart_delay = 600
        end

    elseif self.state == STATE_STOPPING then
        -- We stopped it
        self.state = STATE_STOPPED
        log:info('%s stopped', self.name)

        if self.restart then
            log:info('Start was requested, restarting app: %s', self.name)
            self.restart = false
            self:start()
        end

        self:_notify_stop_waiters()
    else
        log:error('App:_on_stopped when already in stopped state: %s. '
                .. 'Exiting...', self.name)
        os.exit(1)
    end
end

function App:_notify_start_waiters(pid)
    for _, cb in ipairs(self.on_started_callbacks) do
        cb(pid)
    end
    self.on_started_callbacks = {}
end

function App:_notify_stop_waiters()
    for _, cb in ipairs(self.on_stopped_callbacks) do
        cb()
    end
    self.on_stopped_callbacks = {}
end


function M.init(self, name)
    local disabled_apps = config.get_disabled_apps()

    -- JS apps
    local js_apps = config.get_js_apps()
    for app_id, app_obj in pairs(js_apps) do
        local lib_path

        if app_obj['lib_path'] ~= nil then
            lib_path = app_obj['lib_path'] .. ':'
        else
            lib_path = ''
        end
        lib_path = lib_path .. 'js'

        apps[app_id] = App(
            app_id,
            './beepjs ' .. app_obj['path'] .. ' ' ..
                ' --ubus=' .. flags.flags['ubus'] ..
                ' --uciconfig=' .. flags.flags['uciconfig'],
            {
                'BEEPJS_PATH=' .. lib_path,
                'LD_LIBRARY_PATH=.'
            },
            'beep.app.' .. app_id,
            -- "Zombie" apps always start when becoming source (master) and
            -- cannot be killed, merely distracted!
            not not app_obj['zombie'],
            true,
            not not app_obj['zombie'])
    end

    apps['webradio'] = App(
        'webradio',
        './app_generic --ubus=' .. flags.flags['ubus'],
        nil,
        'beep.app.webradio',
        not disabled_apps['webradio'])

    apps['gmrender'] = App(
        'DLNA',
        './gmrender --ubus=' .. flags.flags['ubus'],
        nil,
        'beep.app.DLNA',
        not disabled_apps['dlna'])

    -- Spotify integration omitted from this source release.

    -- Test app
    if flags.flags['vm'] then
        apps['testtarget'] = App(
            'testtarget',
            'lua/app_testtarget.lua --ubus=' .. flags.flags['ubus'],
            nil,
            'beep.app.testtarget',
            false)
    end

    -- Integrations
    apps['nest'] = App(
        'nest',
        '/usr/bin/lua lua/nest.lua --ubus=' .. flags.flags['ubus'],
        nil,
        'beep.integration.nest',
        true,
        nil,
        nil,
        true)
end

return M

