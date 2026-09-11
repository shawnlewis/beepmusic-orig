module(..., package.seeall)

local inspect = require 'inspect'
local stderr_log = require 'log'

require 'beep'

logging = false
replaying = false
logf = nil

filters = {}

function enable_logging(fname)
    logging = true
    logf = io.open(fname, 'w')
end

function log(operation, id, value)
    local now_high, now_low = beep.beep_millis()
    local now_s = beep.beep_millis_to_string(now_high, now_low)

    local msg = now_s
            .. ' ' .. operation
            .. ' ' .. id
    if value then
        local plus_pos = id:find('+')
        local obj_id = id:sub(1, plus_pos - 1)
        if filters[operation] and filters[operation][obj_id] then
            value = filters[operation][obj_id](value)
        end
        local value_s = inspect(value, nil, true)
        msg = msg .. ' ' .. value_s
    end
    msg = msg .. '\n'
    logf:write(msg)
    logf:flush()
end

function add_filter(operation, id, filt)
    if not filters[operation] then
        filters[operation] = {}
    end
    filters[operation][id] = filt
end

obj_reg = {}
seq = 0

function register_obj(id, obj)
    if replaying then
        id = id .. '+' .. seq
        seq = seq + 1
        obj_reg[id] = obj
    end
    return id
end

function run_replay(fname, start_fn_local)
    replaying = true
    logf = io.open(fname, 'r')
end

function next_line()
    local line = logf:read('*line')
    if not line then
        return nil
    end
    local space_pos1 = line:find(' ')
    local time_s = line:sub(1, space_pos1 - 1)
    local space_pos2 = line:find(' ', space_pos1 + 1)
    local operation = line:sub(space_pos1 + 1, space_pos2 - 1)
    local space_pos3 = line:find(' ', space_pos2 + 1)

    local val_s = nil
    local val = nil
    if not space_pos3 then
        space_pos3 = line:len() + 1
    else
        val_s = line:sub(space_pos3 + 1, line:len())
        local eval_fn = loadstring('return ' .. val_s)
        val = eval_fn()
    end
    local id = line:sub(space_pos2 + 1, space_pos3 - 1)

    return operation, id, val
end
