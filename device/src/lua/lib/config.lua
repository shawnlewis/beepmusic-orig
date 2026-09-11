require 'uci'

local flags = require 'flags'
local log = require 'log'

local M = {}

flags.add('uciconfig', true, './')

-- TODO: how should apps be named, namespaced?
local cursor = uci.cursor()

function M.get_cursor()
    ensure_inited()
    return cursor
end

local inited = false

function init()
    cursor:set_confdir(flags.flags['uciconfig'])
    log:info('Using uciconf file in ' .. cursor:get_confdir())
end

function ensure_inited()
    if not inited then
        inited = true
        init()
    end
end

function M._get(config_name, option_name)
    ensure_inited()
    return cursor:get('beep_' .. config_name, 'main', option_name)
end

function M._set(config_name, option_name, value)
    ensure_inited()
    local config_file = 'beep_' .. config_name
    cursor:set(config_file, 'main', option_name, value)
    cursor:save(config_file)
    cursor:commit(config_file)
end

function M.data_get(option_name)
    return M._get('data', option_name)
end

function M.data_set(option_name, value)
    return M._set('data', option_name, value)
end

function M.static_get(option_name)
    return M._get('static', option_name)
end

function M.device_get(option_name)
    return M._get('device', option_name)
end

function M.devel_get(option_name)
    return M._get('devel', option_name)
end

-- Return a set of enabled apps: {bla=1,
function M.get_disabled_apps()
    ensure_inited()
    local disable_prefix = "disable_app_"
    local prefix_len = #disable_prefix

    local disabled_apps = {}
    local all_options = cursor:get_all('beep_devel', 'main')
    for optname, optval in pairs(all_options) do
        if string.sub(optname, 1, prefix_len) == disable_prefix then
            local app_name = string.sub(optname, prefix_len + 1)
            log:info('Disabling app %s', app_name)
            disabled_apps[app_name] = true
        end
    end
    return disabled_apps
end

function M.get_js_apps()
    ensure_inited()

    local js_apps = {}
    local uci_apps = cursor:get_all('beep_apps')
    if not uci_apps then
        log:warn('No beep_apps config file found in --uciconfig directory')
        return {}
    end
    for app_id, app_obj in pairs(uci_apps) do
        if (app_obj['display_name'] == nil) or
            (app_obj['path'] == nil) then
            log:warn('Invalid JS app %s', app_id)
        else
            log:info('Found JS app %s at %s', app_id, app_obj['path'])
            js_apps[app_id] = deepcopy(app_obj)
        end
    end

    return js_apps
end

function M.get_station_info()
    ensure_inited()
    local ssid = cursor:get('wireless.@wifi-iface[-1].ssid')
    local key = cursor:get('wireless.@wifi-iface[-1].key') or ''
    return ssid, key
end

return M
