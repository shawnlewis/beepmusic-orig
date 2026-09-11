require 'ubus'
require 'uloop'


local flags = require 'flags'
local inspect = require 'inspect'
local log = require 'log'

DEFAULT_URELAY_PORT = 29418

flags.add('ubus', true)

local URELAY_DELAY = nil


UBUS_TIMEOUT_DEFAULT_MILLIS = 15000

UBUS_STATUS_METHOD_NOT_FOUND = 3
UBUS_STATUS_NOT_FOUND = 4
UBUS_STATUS_TIMEOUT = 7

BEEP_UBUS_ERROR_DISTRIBUTOR_INVALID_TOKEN = 1
BEEP_UBUS_ERROR_DISTRIBUTOR_STOPPED = 2
BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK = 3

BEEP_UBUS_ERROR_DISTRIBUTOR_STREAM_FAILURE = 10

-- This *MUST* agree with the constant in lib/beep/beep_ubus.h
BEEP_UBUS_ERROR_ARGS = 10000

-- Response staggering range, in ms
local PING_EVENT = 'beep.ping'

local _conn
local _agent_name

function get_ubus_conn()
    return _conn
end

local function on_ping(event, msg)
    ubus_call(_conn, 'beep.health', 'pong', {agent = _agent_name})
end

function beep_ubus_connect(agent_name, timeout)
    if not agent_name then
        log:error("agent_name parameter is required for beep_ubus_connect")
        os.exit(1)
    end

    _conn = ubus.connect(flags.flags['ubus'], timeout)
    if not _conn then
        local ubus_name = 'default'
        if flags.flags['ubus'] then
            ubus_name = flags.flags['ubus']
        end
        log:error("Failed to connect to ubus: " .. ubus_name)
        os.exit(1)
    end

    _agent_name = agent_name

    if _conn:listen(PING_EVENT, on_ping) == nil then
        log:error('Couldn\'t install ping event listener. Exiting...')
        os.exit(1)
    end
    return _conn
end

function beep_success(result)
    if result == nil then
        -- Force the result to be a BLOBMSG_TABLE rather than BLOBMSG_ARRAY,
        -- so we can parse it with beep_ubus.c:beep_parse_response.
        -- TODO: Get rid of this hack.
        result = {__unused= 0}
    end
    return {
        success=true,
        result=result}
end

function beep_error(error_message, error_code)
    return {
        success=false,
        error_message=error_message or 'Generic error',
        error_code=error_code or 0}
end

function beep_reply(conn, deferred_req, response)
    conn:reply(deferred_req, response)
    conn:complete_deferred(deferred_req, 0)
end

-- TODO: merge this with beep_reply
function beep_reply2(deferred_req, response)
    _conn:reply(deferred_req, response)
    _conn:complete_deferred(deferred_req, 0)
end

EXPECTED_TYPES = {}
EXPECTED_TYPES[ubus.ARRAY] = 'table'
EXPECTED_TYPES[ubus.TABLE] = 'table'
EXPECTED_TYPES[ubus.STRING] = 'string'
EXPECTED_TYPES[ubus.INT64] = 'number'
EXPECTED_TYPES[ubus.INT32] = 'number'
EXPECTED_TYPES[ubus.INT16] = 'number'
EXPECTED_TYPES[ubus.INT8] = 'number'
EXPECTED_TYPES[ubus.BOOLEAN] = 'boolean'


function ubus_error_result(ubus_code)
    return {
        ubus_error_code = ubus_code,
        beep_error_code = nil,
        beep_error_message = nil
    }
end

function beep_error_result(beep_code, beep_message)
    return {
        ubus_error_code = nil,
        beep_error_code = beep_code,
        beep_error_message = beep_message
    }
end

-- returns a ubus handler that checks its arguments against the policy
-- also defers req automatically. you must use beep_reply to respond.
function beep_ubus_method(conn, handler, policy)
    return {
        function(req, msg)
            local error_message = nil
            local function add_error(string)
                if error_message == nil then
                    error_message = ''
                end
                error_message = error_message .. string
            end
            for arg, arg_type in pairs(policy) do
                if arg == '__unused' then
                    -- do nothing
                elseif msg[arg] == nil then
                    add_error(string.format('Arg required: %s', arg))
                else
                    local expected_type = EXPECTED_TYPES[arg_type]
                    if expected_type == nil then
                        add_error(string.format('Unknown type: %d', arg_type))
                    elseif expected_type ~= type(msg[arg]) then
                        add_error(string.format(
                                'Arg "%s" should be of type %s',
                                arg, expected_type))
                    end
                end
            end
            if error_message then
                conn:reply(req, beep_error(error_message))
            else
                local deferred_req = conn:defer(req)
                handler(deferred_req, msg)
            end
        end,
        policy
    }
end


-- Optimization: this takes a function that produces an error message,
-- rather than the error message itself. That way we can lazily produce
-- the error message only if it's needed. We were previously, calling inspect
-- on complex table structures for every ubus call, which was slow on the
-- device.
function process_ubus_response(ubus_error_code, response, error_message_fn)
    local result
    local errors
    if ubus_error_code ~= nil then
        errors = ubus_error_result(ubus_error_code)
        result = nil
    elseif response == nil then
        log:error('Invalid ubus_call result: '
                .. 'No ubus error, and no beep response')
        errors = beep_error_result(-1, 'Programming error')
        result = nil
    elseif response.success == nil then
        log:error('Invalid ubus call result: Response missing success field')
        errors = beep_error_result(-1, 'Programming error')
        result = nil
    elseif response.success then
        result = response.result or {}
        errors = nil
    else
        result = nil
        errors = beep_error_result(response.error_code, response.error_message)
        if not (errors.beep_error_code and errors.beep_error_message) then
            log:error('Invalid ubus call result: ' ..
                    'response.success is false, but response is missing ' ..
                    'error_code or error_message')
        end
    end

    return result, errors
end

function ubus_call_log_error(prefix, errors, options)
    if errors.ubus_error_code then
        if (errors.ubus_error_code == UBUS_STATUS_METHOD_NOT_FOUND
                and options.method_not_found_ok)
           or (errors.ubus_error_code == UBUS_STATUS_NOT_FOUND
                and options.not_found_ok) then
            --log:info(prefix ..  'ubus error code: %s',
            --        errors.ubus_error_code)
        elseif errors.ubus_error_code == UBUS_STATUS_TIMEOUT
                and options.timeout_ok then
            --log:info('TIMEOUT')
        else
            log:error(prefix ..  'ubus error code: %s',
                    errors.ubus_error_code)
        end
    elseif errors.beep_error_code then
        log:error(prefix ..
                'Beep error code: ' .. errors.beep_error_code ..
                ' message: ' .. errors.beep_error_message)
    end
end

-- on success returns a handle for the listener that can be used to unsubscribe
-- on error returns the same format as ubus_call below.
function ubus_listen(ubus, event, callback)
    local result, ubus_error_code = ubus:listen(PING_EVENT, on_ping)
    if result then
        return result
    else
        log:error('ubus:listen failed, exiting...')
        os.exit(1)
    end
end

-- call a ubus method
-- Returns
-- on success: result, nil
-- on failure: nil, {'ubus_error_code': ...,
--                   'beep_error_code': ...,
--                   'beep_error_message': ...}
function ubus_call(ubus, path, method, args, done_cb, options)
    args = args or {}
    options = options or {}
    --log:info(string.format('CALLING UBUS METHOD: %s %s %s', path, method, inspect(args)))

    local error_message_fn = function()
        return string.format(
            'Command %s:%s %s failed.', path, method, inspect(args, nil, true))
    end

    local function handle_done(ubus_result, ubus_error_code)
        local result, error_response = process_ubus_response(
                ubus_error_code, ubus_result, error_message_fn)
        if not result then
            local prefix = string.format(
                'Command %s:%s %s failed. ',
                path, method, inspect(args, nil, true))
            -- replace any '%' signs with '<percent>', if there was a '%' in
            -- path, method or args we'd screw up the log format string
            -- (and end up with a stacktrace).
            prefix = string.gsub(prefix, '%%', '<percent>')
            ubus_call_log_error(prefix, error_response, options)
        end
        if done_cb then
            done_cb(result, error_response)
        end
    end
    local ubus_error_code = ubus:call_async(path, method, args, handle_done,
            options['timeout_millis'] or UBUS_TIMEOUT_DEFAULT_MILLIS)
    if ubus_error_code then
        log:warn('GOT UBUS ERROR: %s', ubus_error_code)
        handle_done(nil, ubus_error_code)
    end
end

function _ubus_relay_call(ubus, id, path, method, args, done_cb)
    args = args or {}
    local relay_invoke_args = {
        id = id,
        path = path,
        method = method,
        msg = args
    }
    ubus_call(ubus, 'urelay', 'invoke', relay_invoke_args,
        function(result, errors)
            if result ~= nil then
                if result.error_code and result.error_message then
                    -- This is actually a nested error; there won't be a result
                    -- field
                    log:warn('Relay nested error: %s', result)
                elseif result.result == nil then
                    log:error('Invalid relay result: %s', result)
                end

                -- pull out nested result
                local error_message_fn = function()
                    return string.format(
                            'Remote command %s:%s:%s %s failed.',
                            id, path, method, inspect(args, nil, true))
                end

                local inner_result, error_response =
                        process_ubus_response(
                                nil, result, error_message_fn)
                if done_cb then
                    done_cb(inner_result, error_response)
                end
            else
                if done_cb then
                    done_cb(result, errors)
                end
            end
        end)
end

function ubus_relay_call(ubus, id, path, method, args, done_cb)
    if URELAY_DELAY then
        uloop.timer(function()
            _ubus_relay_call(ubus, id, path, method, args, done_cb)
        end, URELAY_DELAY)
    else
        _ubus_relay_call(ubus, id, path, method, args, done_cb)
    end
end

function beep_send_state(ubus, component, event_type, event_data, state)
    ubus:send_event('beep.state.' .. component .. '._local_', {
        event_type = event_type,
        event_data = event_data or {},
        state = state
    })
end
