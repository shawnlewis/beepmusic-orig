-- log:debug log:info, log:warn, log:error can all be called with a
-- format string followed by varargs (similar to string.format). All
-- varargs are coerced to strings automatically, so
-- log:info('A table: %s', {a={b=c=5}}) is valid.
require 'beep'
local inspect = require 'inspect'

local log = {}

local category
local log_output = io.stderr

local ALLOWED_REPEATS = 10
local previous_fmt = nil
local repeat_count = 1

function log:init(cat)
    category = cat
end

-- Pass in the result of io.output(<file_name>) or io.stderr
function log:set_output(output)
    log_output = output
end

local function prefix(color, level)
    local now_high, now_low = beep.beep_millis()
    local now_s = beep.beep_millis_to_string(now_high, now_low)
    -- level is padding to be six width, left-justified
    while #level < 6 do
        level = level .. ' '
    end
    local pre = string.format('%s%s %s %s',
            color, beep.beep_millis_to_log_time_string(now_high, now_low),
            now_s, string.upper(level))
    if category then
        pre = pre .. ' ' .. category
    end
    pre = pre .. ' - '
    return pre
end

-- Can be replaced in testing to capture log output
function log.log_write(color, level, line)
    log_output:write(prefix(color, level) .. line .. '\27[0m\n')
end

local function log_write_wrapper(color, level, fmt, ...)
    if fmt == previous_fmt then
        repeat_count = repeat_count + 1
        if repeat_count > ALLOWED_REPEATS then
            return
        end
    else
        if repeat_count > ALLOWED_REPEATS then
            log.log_write('\27[0;32m', 'warn', 'Suppressed '
                .. tostring(repeat_count - ALLOWED_REPEATS) ..
                ' repeated messages for format: ' .. previous_fmt)
        end
        previous_fmt = fmt
        repeat_count = 1
    end

    fmt = string.gsub(fmt, '%%.', '%%s')
    local fields = {}
    for i, v in ipairs({...}) do
        table.insert(fields, inspect(v, nil, true))
    end
    local line
    if #fields == 0 then
        line = fmt
    else
        line = string.format(fmt, unpack(fields))
    end
    -- replace with \\n since that's what inspect seems to do.
    line = line.gsub(line, '\n', '\\\\n')
    log.log_write(color, level, line)
end

function log:debug(s, ...)
    log_write_wrapper('\27[0;34m', 'debug', s, ...)
end

function log:info(s, ...)
    log_write_wrapper('\27[0;33m', 'info', s, ...)
end

function log:warn(s, ...)
    log_write_wrapper('\27[0;32m', 'warn', s, ...)
end

function log:error(s, ...)
    log_write_wrapper('\27[0;31m', 'error', s, ...)
end

function log.disable_repeat_suppression()
    ALLOWED_REPEATS = 10000000
end

return log
