local inspect = require 'inspect'
local luassert_util = require 'luassert.util'
require 'util'

-- Make requiring these modules a noop.
package.preload.ubus = function() end
package.preload.uloop = function() end
package.preload.stream = function() end

local M = {}

local VERBOSE = false

local UbusConnection = {}

function UbusConnection:_init(uloop, object_name)
    self._uloop = uloop
    self._object_name = object_name
    self._call_handlers = {}
    self._listeners = {}
    self._next_req_num = 0
    self._replies = {}
    self._events = {}
end

function UbusConnection:listen(event_type, event_cb)
    self._listeners[event_type] = event_cb
    return true
end

function UbusConnection:add(objects)
    self._objects = objects[self._object_name]
    assert(self._objects, 'object name not added: ' .. self._object_name)
end

function UbusConnection:call_async(path, method, args, done_cb)
    if VERBOSE then
        print(string.format('TIME: %d, Got call %s:%s %s',
                self._uloop._state.time, path, method, inspect(args)))
    end
    if not self._call_handlers[path] then
        done_cb(nil, 4)  -- object not found
    elseif not self._call_handlers[path][method] then
        done_cb(nil, 3)  -- method not found
    else
        self._uloop.timer(function()
            if VERBOSE then
                print(string.format('TIME: %d, Running handler for %s:%s',
                        self._uloop._state.time, path, method))
            end
            self._call_handlers[path][method]['handler'](args, done_cb)
        end, self._call_handlers[path][method]['delay'])
    end
end

function UbusConnection:defer(req)
    return req
end

function UbusConnection:complete_deferred()
end

function UbusConnection:reply(req, response)
    if VERBOSE then
        print(string.format('Reply for req %d: %s', req, inspect(response)))
    end
    if not req then
        return
    end
    self._replies[req] = response
end

function UbusConnection:send_event(event, msg)
    if VERBOSE then
        print(string.format('TIME %d. Event sent: %s %s',
                self._uloop._state.time, event, inspect(msg)))
    end
    table.insert(self._events, {
        event = event,
        msg = deepcopy(msg)
    })
end

-- call with method == nil to clear object handler
-- call with handler == nil to clear method handler
-- handler can be a function, or a beep ubus response
function UbusConnection:_set_call_handler(path, method, delay, result)
    assert(path)
    if not method then
        self._call_handlers[path] = nil
        return
    end
    if not self._call_handlers[path] then
        self._call_handlers[path] = {}
    end

    local handler
    if type(result) == 'function' then
        handler = result
    else
        handler = function(msg, done_cb)
            done_cb(result)
        end
    end

    handler = spy.new(handler)

    self._call_handlers[path][method] = {
        delay=delay,
        handler=handler
    }

    return handler
end

function UbusConnection:_call(method, args)
    assert(self._objects[method], 'method not provided: ' .. method)
    local func = self._objects[method][1]
    local signature = self._objects[method][2]
    local req_num = self._next_req_num
    self._next_req_num = self._next_req_num + 1
    func(req_num, args)
    return req_num
end

function UbusConnection:_get_reply(req)
    return self._replies[req]
end

function UbusConnection:_clear_events()
    self._events = {}
end

M.new_ubus_connection = function(...)
    local o = {}
    setmetatable(o, UbusConnection)
    UbusConnection.__index = UbusConnection
    o:_init(...)
    return o
end

M.ubus = function(arg)
    return {
        ARRAY = 1,
        TABLE = 2,
        STRING = 3,
        INT64 = 4,
        INT32 = 5,
        INT16 = 6,
        INT8 = 7,
        BOOLEAN = 8,

        connect = function()
            return arg.connect_result
        end
    }
end


M.uloop = function()
    local timers = {}
    local state = {time = 0}

    -- finds the index of the timer with the lowest timeout.
    local function find_min_index_timer()
        assert(#timers > 0)
        local min_index = 1
        local min_timeout = timers[1].timeout_ms
        for i, timer in ipairs(timers) do
            if timer.timeout_ms < min_timeout then
                min_timeout = timer.timeout_ms
                min_index = i
            end
        end
        return min_index, timers[min_index]
    end

    return {
        init = function() end,

        run = function() end,

        timer = function(timeout_cb, timeout_ms)
            assert(timeout_ms >= 0)
            table.insert(timers, {
                timeout_cb= timeout_cb,
                timeout_ms= timeout_ms
            })
        end,

        _timers = timers,
        _state = state,

        -- TODO: make a unit test for this.
        _timer_advance = function(ms)
            -- timer callbacks can add new timers, so we need to run
            -- them one timestep at a time.
            local remaining = ms
            while #timers > 0 do
                local min_index, min_timer = find_min_index_timer()
                -- advancing by min_timer.timeout_ms can cause multiple
                -- timers to fire if there are others that have the
                -- same timeout. this code accounts for that.
                if min_timer.timeout_ms <= remaining then
                    state.time = state.time + min_timer.timeout_ms
                    remaining = remaining - min_timer.timeout_ms
                    -- advance them all by timer.timeout_ms
                    for i = #timers, 1, -1 do
                        local timer = timers[i]
                        timer.timeout_ms =
                                timer.timeout_ms - min_timer.timeout_ms
                        assert(timer.timeout_ms >= 0)
                        if timer.timeout_ms == 0 then
                            timer.timeout_cb()
                            table.remove(timers, i)
                        end
                    end
                else
                    break
                end
            end

            -- advance by leftover remaining time.
            state.time = state.time + remaining
            for i, timer in ipairs(timers) do
                timer.timeout_ms = timer.timeout_ms - remaining
            end
        end
    }
end

M.stream = function()
    return {
        new_stream = function(ip, port, connected_callback)
            connected_callback(true)
            return {
                start = function() return true end,
                track_begin = function() return true end
            }
        end
    }
end

return M
