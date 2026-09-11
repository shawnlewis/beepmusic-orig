module(..., package.seeall)

require 'beep_ubus'

local function set(global, key, val, on_done)
    if (not key) or (not val) then
        log:warn('Missing/invalid parameters to call to set')
        return
    end

    local function done_cb(result)
        if not on_done then
            return
        end

        if not result then
            on_done(false)
        else
            on_done(true)
        end
    end

    ubus_call(get_ubus_conn(), 'beep.cloud', 'set', {
            global = global,
            key = key,
            value = val}, done_cb)
end

local function get(global, key, on_done)
    if not key then
        log:warn('Missing/invalid key to call to get')
        return
    end

    local function done_cb(result)
        if not on_done then
            return
        end

        if not result then
            on_done(nil)
        elseif result.value == "" then
            on_done(nil)
        else
            on_done(result.value)
        end
    end

    ubus_call(get_ubus_conn(), 'beep.cloud', 'get', {
        global = global,
        key = key}, done_cb)
end

local function delete(global, key, on_done)
    if not key then
        return
    end

    local function done_cb(result)
        if not on_done then
            return
        end

        if not result then
            on_done(false)
        else
            on_done(true)
        end
    end

    ubus_call(get_ubus_conn(), 'beep.cloud', 'delete', {
        global = global,
        key = key}, done_cb)
end

function set_group(key, val, on_done)
    set(false, key, val, on_done)
end

function get_group(key, on_done)
    get(false, key, on_done)
end

function delete_group(key, on_done)
    delete(false, key, on_done)
end

function set_global(key, val, on_done)
    set(true, key, val, on_done)
end

function get_global(key, on_done)
    get(true, key, on_done)
end

function delete_global(key, on_done)
    delete(true, key, on_done)
end
