-- This ended up being 5x slower on the device

module(..., package.seeall)

local inspect = require 'inspect'

require 'uloop'
require 'util'

local DEBUG_TIMING = false

local bdebug = require 'bdebug'
local log = require 'log'

local next_run = nil
local to_start = {}
local waiters = {}
local have_events_for = {}

local GO_DONE = '__go_done'

local function _as_table(...)
    return arg
end

function go(func)
    local result_ch = Channel(Buffer(1))
    table.insert(to_start, coroutine.create(function()
        result_ch:put(_as_table(func()))
    end))
    return unpacking_channel(result_ch)
end

function go_loop(func)
    go(function()
        while true do
            func()
        end
    end)
end

function run()
    next_run = nil

    local run_threads = {}
    for _, thread in ipairs(to_start) do
        table.insert(run_threads, thread)
    end
    to_start = {}

    --print('HAVE EVENTS FOR')
    --print(inspect(have_events_for))
    --print('WAITERS')
    --print(inspect(waiters))
    for _, obj in ipairs(have_events_for) do
        local waiting_threads = waiters[obj]
        if waiting_threads then
            for _, thread in ipairs(waiting_threads) do
                if not array_contains(run_threads, thread) then
                    table.insert(run_threads, thread)
                end
            end
        end
    end
    --print('RUN THREADS')
    --print(inspect(run_threads))

    local current_waiters = waiters
    waiters = {}

    for i, t in ipairs(run_threads) do
        _remove_waiter(current_waiters, t)
        local caller, start_time_h, start_time_l
        if DEBUG_TIMING then
            caller = bdebug.coroutine_yield_line(t)
            start_time_h, start_time_l = beep.beep_millis()
        end
        local success, result = coroutine.resume(t)
        if not success then
            io.stderr:write(debug.traceback(t, result) .. '\n')
            log:error('Coroutine error: %s. Exiting...', result)
            os.exit(1)
        end
        --if coroutine.status(t) == 'dead' then
        --    -- insert at front so the list is reversed
        --    table.insert(remove_indexes, 1, i)
        --end
        if DEBUG_TIMING then
            local end_time_h, end_time_l = beep.beep_millis()
            local _, delta = beep.beep_millis_sub(
                end_time_h, end_time_l, start_time_h, start_time_l)
            print('ASYNC TIMING: ' .. caller .. ' ' .. tostring(delta))
        end
    end

    --print('POST WAITERS')
    --print(inspect(waiters))

    _merge_waiters(waiters, current_waiters)
end

function schedule_run(event_obj)
    if not next_run then
        have_events_for = {}
        next_run = uloop.timer(run, 0)
    end
    table.insert(have_events_for, event_obj)
end

function start()
    schedule_run()
end

function timeout(millis)
    local channel = Channel(Buffer())
    uloop.timer(function()
        channel:put(true)
    end, millis)
    return channel
end

-- TODO: deal with closed channels
function alts(take_channels, put_channels)
    if not coroutine.running() then
        print(debug.traceback())
        log:error('May not call alts from main thread. Exiting...')
        os.exit(1)
    end
    if not take_channels and not put_channels then
        print(debug.traceback())
        log:error('Got nil for take_channels and put_channels. Exiting...')
        os.exit(1)
    end
    while true do
        new_waiters = {}
        if take_channels then
            for _, take_channel in ipairs(take_channels) do
                if take_channel.buffer.count > 0 then
                    return take_channel
                end
                table.insert(new_waiters, take_channel.buffer)
            end
        end
        if put_channels then
            for _, put_channel in ipairs(put_channels) do
                if put_channel.buffer.count < put_channel.size then
                    return put_channel
                end
                table.insert(new_waiters, put_channel.buffer)
            end
        end

        _yield(new_waiters)
    end
end

-- returns a :take only channel that unpacks the result of :take'ing from the wrapped
-- channel
function unpacking_channel(wrap_ch)
    local t = {}
    t.take = function()
        return unpack(wrap_ch:take())
    end
    return t
end

function _add_waiters(waiters, waiter, waiting_on)
    --print('ADDING WAITING', waiter)
    --print('  WAITING ON ', inspect(waiting_on))
    --print('  TO WAITERS ', inspect(waiters))
    for _, obj in ipairs(waiting_on) do
        if not waiters[obj] then
            --print('DIDN\'T HAVE')
            waiters[obj] = {}
        end
        if not array_contains(waiters[obj], waiter) then
            --print('DIDN\'T CONTAIN')
            table.insert(waiters[obj], waiter)
        end
    end
end

function _remove_waiter(waiters, waiter)
    for _, waiters_list in pairs(waiters) do
        local remove_indexes = {}
        for i, w in ipairs(waiters_list) do
            if w == waiter then
                -- build backwards
                table.insert(remove_indexes, 1, i)
            end
        end
        for _, i in ipairs(remove_indexes) do
            table.remove(waiters_list, i)
        end
    end
end

function _merge_waiters(into, from)
    for waiting_on, waiter_list in pairs(from) do
        for _, waiter in ipairs(waiter_list) do
            _add_waiters(into, waiter, {waiting_on})
        end
    end
end

-- Requires an array of objects that are being waited on.
function _yield(waiting_on)
    local thread = coroutine.running()
    --print('YIELD ON THREAD', thread)
    if not thread then
        log:error('Trying to yield on main thread. Exiting...')
        os.exit(1)
    end
    _add_waiters(waiters, thread, waiting_on)
    coroutine.yield()
end


class('Mult')
function Mult:_init(channel)
    self.channel = channel
    self.taps = {}
    go(function()
        while true do
            local val = self.channel:take()
            for _, channel in ipairs(self.taps) do
                channel:put(val)
            end
        end
    end)
end

function Mult:tap(channel)
    if not channel then
        channel = Channel()
    end
    table.insert(self.taps, channel)
    return channel
end

function merge(channels)
    local merged_ch = Channel(Buffer())
    -- need a co-routine to do this?
    go(function()
        while true do
            local watch_channels = {}
            local all_closed = true
            for _, ch in ipairs(channels) do
                if not merged_ch.closed then
                    table.insert(watch_channels, ch)
                    all_closed = false
                end
            end
            if all_closed then
                break
            end
            local event_ch = alts(watch_channels)
            merged_ch:put(event_ch:take())
        end
    end)

    return merged_ch
end

-- standard sized buffer (blocking), or infinite

class('Buffer')
function Buffer:_init(size)
    self.size = size
    self.count = 0
    self.vals = {}
end

function Buffer:put(val)
    if not coroutine.running() and self.size then
        print(debug.traceback())
        log:error('May not call :put on sized buffer from main thread. Exiting...')
        os.exit(1)
    end

    while self.count == self.size do
        _yield({self})
    end
    table.insert(self.vals, val)
    self.count = self.count + 1
    schedule_run(self)
end

function Buffer:take()
    if not coroutine.running() then
        print(debug.traceback())
        log:error('May not call :take on buffer from main thread. Exiting...')
        os.exit(1)
    end
    while self.count == 0 do
        _yield({self})
    end
    schedule_run(self)
    self.count = self.count - 1
    return table.remove(self.vals, 1)
end

function Buffer:take_async()
    if self.count == 0 then
        return nil
    end
    schedule_run(self)
    self.count = self.count - 1
    return table.remove(self.vals, 1)
end

class('SlidingBuffer', Buffer)
function SlidingBuffer:_init(...)
    Buffer._init(self, ...)
end

function SlidingBuffer:put(val)
    if self.count == self.size then
        -- drop one
        table.remove(self.vals, 1)
        self.count = self.count - 1
    end

    table.insert(self.vals, val)
    self.count = self.count + 1
    schedule_run(self)
end


class('DroppingBuffer', Buffer)
function DroppingBuffer:_init(...)
    Buffer._init(self, ...)
end

function DroppingBuffer:put(val)
    if self.count == self.size then
        -- drop it
        return
    end

    table.insert(self.vals, val)
    self.count = self.count + 1
    schedule_run(self)
end


-- Channel

class('Channel')
function Channel:_init(buffer)
    if not buffer then
        buffer = Buffer(1)
    end
    self.buffer = buffer
    self.closed = false
end

function Channel:close()
    self.closed = true
end

function Channel:put(val)
    if self.closed then
        return nil
    end

    self.buffer:put(val)

    return true
end

function Channel:take()
    if self.closed and self.buffer.count == 0 then
        return nil
    end

    return self.buffer:take()
end

function Channel:take_async()
    if self.closed and self.buffer.count == 0 then
        return nil
    end
    return self.buffer:take_async()
end
