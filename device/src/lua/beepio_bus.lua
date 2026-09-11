module(..., package.seeall)

require 'util'

require 'beepio_clock'

local log = require 'log'

local all_handlers = {}

local function find_handler(h)
    for event_type, prio_handlers in pairs(all_handlers) do
        for prio, handlers in pairs(prio_handlers) do
            for i, handler in ipairs(handlers) do
                if h == handler then
                    return event_type, prio, i
                end
            end
        end
    end
    return nil
end

function add_handler(event_type, priority, handler)
    if find_handler(handler) then
        log:error('Handler already registered')
        log:error(debug.traceback())
        os.exit(1)
    end
    if not all_handlers[event_type] then
        all_handlers[event_type] = {}
    end
    if not all_handlers[event_type][priority] then
        all_handlers[event_type][priority] = {}
    end
    table.insert(all_handlers[event_type][priority], handler)
end

function remove_handler(handler)
    local event_type, prio, i = find_handler(handler)
    if not event_type then
        return
    end
    table.remove(all_handlers[event_type][prio], i)
end

function send_event(event_type, arg1, arg2)
    local arg1_s = arg1
    if type(arg1) == 'table' and arg1.__name then
        arg1_s = arg1.__name
    end
    local time = beepio_clock.millis()
    if event_type == 'hold' then
        -- don't log hold events, they're spammy
    elseif arg2 then
        log:debug('Sending event: %s %s %s %s', time, event_type, arg1_s, arg2)
    elseif arg1 then
        log:debug('Sending event: %s %s %s', time, event_type, arg1_s)
    else
        log:debug('Sending event: %s %s', time, event_type)
    end
    local prio_handlers = all_handlers[event_type]
    if not prio_handlers then
        return
    end
    for prio = 1, 10 do
        if prio_handlers[prio] then
            for _, handler in ipairs(prio_handlers[prio]) do
                if handler(time, arg1, arg2) then
                    -- stop propagation
                    return true
                end
            end
        end
    end
    return false
end
