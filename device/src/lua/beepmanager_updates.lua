module(..., package.seeall)
require 'beep_ubus'

local config = require 'config'
local device = require 'device'
local log = require 'log'

VERSION_FILE_PATH = config.devel_get('version_file_path') or '/beep/platform/VERSION'


local UPDATE_CHECK_INTERVAL_S = 30
local UPDATE_NOTIFICATION_FILE = '/tmp/__UPDATE_READY'
local PAUSED_TIME_REQUIRED_S = 10 * 60

--local UPDATE_CHECK_INTERVAL_S = 1
--local PAUSED_TIME_REQUIRED_S = 30

local paused_time = nil

local function file_exists(name)
    local f = io.open(name, 'r')
    if f ~= nil then
        io.close(f)
        return true
    else
        return false
    end
end

local function apply_update(other_version)
    log:info('Running beepupdate to check for an update!')

    local args = {'-S', '-b', '-x', '/beep/beepupdate'}

    if other_version then
        table.insert(args, '--')
        table.insert(args, '--requested')
        table.insert(args, other_version)
    end

    -- Use start-stop-daemon to disown beepupdate.
    uloop.process('/sbin/start-stop-daemon', args,
            nil, nil,
            function()
                log:debug('beepupdate exited.')
            end)
end

local function check_for_update()
    local have_update = file_exists(UPDATE_NOTIFICATION_FILE)
    local current_time = os.time()
    --local log_paused_time = -1
    --if paused_time then
    --    log_paused_time = paused_time
    --end
    --log:debug('Checking for update. have_update: %s %s %s', have_update, current_time, log_paused_time)
    if have_update and paused_time
            and current_time - paused_time > PAUSED_TIME_REQUIRED_S then
        -- this will kill us because beep services will be restarted
        log:info('Update ready and we\'ve been paused long enough.')
        apply_update()
    end
    uloop.timer(check_for_update, UPDATE_CHECK_INTERVAL_S * 1000)
end

function current_version()
    local f = io.open(VERSION_FILE_PATH, 'r')
    local version
    if f then
        version = f:read('*line')
    end
    return version
end

function do_version_check(local_config, global_config)
    local other_version = nil
    for dev_id, params in pairs(global_config) do
        if local_config.sink_id == params.sink
                and params.version ~= local_config.version then
            other_version = params.version
            break
        end
    end
    if other_version then
        log:info('Found device in group with a different version.')
        apply_update(other_version)
    end
end

function init()
    local local_dev =  device.new(get_ubus_conn(), 'beepmanager_updates', true,
        'local', nil, 'local', function()
            log:error('beepmanager_updates lost local connection.')
            os.exit(1)
        end)

    local_dev:subscribe('playnet', function(key, msg)
        -- Depending on playnet 'paused', which it may not be if we get into
        -- a bad state, could also check that written_track_time is not
        -- advancing, but playnet only sends updates when 'paused' changes
        -- for now.
        if not msg.event_type then
            -- initial state
            if msg.state.paused == 1 then
                paused_time = os.time()
            else
                paused_time = nil
            end
        else
            if msg.state.paused == 1 then
                paused_time = os.time()
            else
                paused_time = nil
            end
        end
    end)

    uloop.timer(check_for_update, 1)
end
