module(..., package.seeall)

function coroutine_yield_line(thread)
    local tb = debug.traceback(thread, '', 2)
    local lines = iter_to_array(string.gmatch(tb, '[^\n]+'))
    if not lines[2] then
        return ''
    end
    local call_line = string_strip(lines[2])
    local call_split = iter_to_array(string.gmatch(call_line, '[^:]+'))
    return call_split[1] .. ':' .. call_split[2]
end
