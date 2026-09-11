module(..., package.seeall)

require 'uloop'
require 'util'

local DEBUG_TIMING = false

local bdebug = require 'bdebug'
local replay_log = require 'replay_log'
local log = require 'log'

local next_run = nil
local threads = {}

local GO_DONE = '__go_done'

local function _as_table(...)
    return arg
end

function go(func)
    local result_ch = Channel(Buffer(1))
    table.insert(threads, coroutine.create(function()
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

-- Explicit full sweep garbage collection
-- In some cases, Lua will allow memory usage to spike considerably before
-- cleaning up, which has led to OOM failures on the device.  Calling
-- collectgarbage() regularly greatly reduces these surges in memory use,
-- at the cost of some CPU.
--
-- gc_divisor should be set as high as tolerable.
local explicit_gc_enabled = false
local gc_divisor = 100
local run_count = 0

function set_gc_enabled(enabled)
    explicit_gc_enabled = enabled
end

function set_gc_divisor(n)
    gc_divisor = n
end

function run()
    if explicit_gc_enabled then
        run_count = run_count + 1
        if run_count % gc_divisor == 0 then
            collectgarbage()
        end
    end

    local remove_indexes = {}
    for i, t in ipairs(threads) do
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
        if coroutine.status(t) == 'dead' then
            -- insert at front so the list is reversed
            table.insert(remove_indexes, 1, i)
        end
        if DEBUG_TIMING then
            local end_time_h, end_time_l = beep.beep_millis()
            local _, delta = beep.beep_millis_sub(
                end_time_h, end_time_l, start_time_h, start_time_l)
            print('ASYNC TIMING: ' .. caller .. ' ' .. tostring(delta))
        end
    end
    for _, i in ipairs(remove_indexes) do
        table.remove(threads, i)
    end
    next_run = nil
end

function schedule_run()
    if not next_run then
        next_run = uloop.timer(run, 0)
    end
end

function start()
    schedule_run()
end

function timeout(millis, external_id)
    local channel = Channel(Buffer(), external_id)
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
        if take_channels then
            for _, take_channel in ipairs(take_channels) do
                if take_channel.buffer.count > 0 then
                    return take_channel
                end
            end
        end
        if put_channels then
            for _, put_channel in ipairs(put_channels) do
                if put_channel.buffer.count < put_channel.size then
                    return put_channel
                end
            end
        end
        coroutine.yield()
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
        coroutine.yield()
    end
    table.insert(self.vals, val)
    self.count = self.count + 1
    schedule_run()
end

function Buffer:take()
    if not coroutine.running() then
        print(debug.traceback())
        log:error('May not call :take on buffer from main thread. Exiting...')
        os.exit(1)
    end
    while self.count == 0 do
        coroutine.yield()
    end
    schedule_run()
    self.count = self.count - 1
    return table.remove(self.vals, 1)
end

function Buffer:take_async()
    if self.count == 0 then
        return nil
    end
    schedule_run()
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
    schedule_run()
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
    schedule_run()
end


-- Channel

local replay_id = 0

class('Channel')
function Channel:_init(buffer, external_id)
    if not buffer then
        buffer = Buffer(1)
    end
    self.buffer = buffer
    self.closed = false
    if external_id then
        self.external_id = replay_log.register_obj(external_id, self)
    end
end

function Channel:close()
    self.closed = true
end

function Channel:put(val)
    if self.closed then
        return nil
    end

    if replay_log.logging and self.external_id then
        replay_log.log('channel_put', self.external_id, val)
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
