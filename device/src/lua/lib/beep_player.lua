local M = {}

-- Overestimate by a lot.
local STREAM_COMMAND_SIZE = 64 * 1024

local NUM_START_TIMES = 20

require 'beep_ubus'
require 'stream'
require 'util'
local device = require 'device'
local log = require 'log'
local replay_log = require 'replay_log'

local inspect = require 'inspect'

local Player = {}

function Player:init(ip, port, id, name, playnet_path, on_ready, on_error)
    self.id = id
    self.name = name
    self.playnet_path = playnet_path or 'beep.playnet'
    self.on_ready = on_ready
    self.has_error = false
    self.on_error = on_error
    self:reset_sync_state()

    -- TODO: This relies on ubus_conn defined in beep_ubus.lua
    self.dev = device.new(get_ubus_conn(), 'distributor', true, ip, port, id,
        function() -- on_error
            log:info('beep_player error for id: %s', self.id)
            self:_on_error(false)
        end)
    self:call('hello', nil,
        function(result)
            if result ~= nil then
                local stream_port = result.stream_port
                log:info('Got stream port ' .. result['stream_port'])
                self._stream = stream.new_stream(ip, stream_port, function(connected)
                    if connected then
                        log:debug('Successfully connected stream %s:%d',
                            ip, stream_port)
                        self:_init2(on_ready)
                    else
                        log:error(string.format(
                            'Couldn\'t connect stream %s:%d',
                            ip, stream_port))
                        self:_init_error(true)
                    end
                end)
                self.state = result.status

                if not self._stream then
                    log:error(string.format(
                        'Couldn\'t connect stream %s:%d',
                        ip, stream_port))
                    self:_init_error(true)
                end
            else
                self:_init_error(false)
            end
        end)
end

function Player:_init2(on_ready)
    -- set the cookie to a value that the distributor will never use, that
    -- way the distributor can ignore state updates from this player until
    -- it has started playing to it.
    self:call('stop', {set_cookie= -2},
        function(result)
            if result ~= nil then
                self.prev_state = {}
                self:log_state()
                self.on_ready(self)
            else
                self:_init_error(false)
            end
        end)
end

function Player:_init_error(is_stream_error)
    if not self.has_error then
        if is_stream_error then
            self.dev:on_error(beep_error_result(-1, 'stream failed'))
        end
        self:close()
        self.has_error = true
        self.on_ready(nil)
    end
end

function Player:_on_error(is_stream_error, is_logic_error)
    if not self.has_error then
        if is_stream_error then
            self.dev:on_error(beep_error_result(-1, 'stream failed'))
        elseif is_logic_error then
            self.dev:on_error(beep_error_result(-1, 'logic error'))
        end
        self:close()
        self.has_error = true
        self.on_error(self)
    end
end

function Player:log_state()
    if replay_log.logging then
        local player_states = {}
        player_states[self.id] = {
            prev_state = self.prev_state,
            state = self.state,
            apparent_start_times = self.apparent_start_times,
            apparent_start_time = self.apparent_start_time
        }
        replay_log.log('player_states', 'player_states+1', player_states)
    end
end

function Player:close()
    if self.stream then
        self._stream:close()
    end

    if self.dev then
        self.dev:cleanup()
    end
end

function Player:reset_sync_state()
    self.apparent_start_time = 0
    self.apparent_start_times = {}
end

function M.is_syncable(player)
    return #player.apparent_start_times == NUM_START_TIMES
end

function Player:on_player_event(msg)
    self.prev_state = self.state
    self.state = msg

    -- Save the actual streambuf_free value, Player modifies streambuf_free
    -- whenever we send command so that we always have a conservative
    -- estimate of what's available.
    self.state.streambuf_free_last_known = self.state.streambuf_free

    -- sync calculations
    -- TODO: This uses lua double math which will be slow on the device.
    --     replace with c lib so we can use int math?
    if self.state.sync_played_time == 0
            or self.state.sync_played_time
               < self.prev_state.sync_played_time then
        -- sync_played_time may be reset to 0 when a new track starts.
        -- If it is currently
        -- 0 then the track hasn't actually started yet. As long as it
        -- remains 0 we keep an empty sync state so the distributor won't
        -- try to skip any players in the group.
        -- If sync_played_time goes backwards (meaning a new track has
        -- started playing or it has wrapped) we clear the state. We won't
        -- always see it go to exactly 0 since it is sampled.
        self:reset_sync_state()
    else
        local start_time =
                self.state.sync_timestamp - self.state.sync_played_time
        table.insert(self.apparent_start_times, start_time)
        if #self.apparent_start_times > NUM_START_TIMES then
            table.remove(self.apparent_start_times, 1)
        end
        self.apparent_start_time = sum(self.apparent_start_times)
                / #self.apparent_start_times
    end

    --log:debug('START TIMES: ' .. self.id .. ' '  .. inspect(self.apparent_start_times) .. ' ' .. self.apparent_start_time)
    --print('START TIMES: ' .. self.id .. ' '  .. inspect(self.apparent_start_times) .. ' ' .. self.apparent_start_time)
end


function Player:call(method, args, done_cb)
    if self.has_error then
        if done_cb then
            done_cb(nil)
        end
    else
        self.dev:call(self.playnet_path, method, args, done_cb)
    end
end

function Player:update_state(on_success, on_error)
    if self.has_error then
        on_error()
    else
        self:call('get_state', {}, function(result, errors)
            if result then
                self:on_player_event(result)
                on_success()
            else
                on_error()
            end
        end)
    end
end

function M.player_can_track_begin(player)
    return player.state.can_st_begin
            and player.state.streambuf_free > STREAM_COMMAND_SIZE
end

function Player:track_begin(audio_type)
    if string.len(audio_type) ~= 1 then
        log:error('Invalid audio_type passed to player:track_begin');
        os.exit()
    end

    self.state.can_st_begin = false
    self.state.streambuf_free =
            self.state.streambuf_free - STREAM_COMMAND_SIZE

    if not self.has_error and self._stream:track_begin(string.byte(audio_type)) then
        return true
    else
        self:_on_error(true)
        return false
    end
end

function M.player_can_track_end(player)
    return player.state.streambuf_free > STREAM_COMMAND_SIZE
end

function Player:track_end()
    self.state.streambuf_free =
            self.state.streambuf_free - STREAM_COMMAND_SIZE
    if not self.has_error and self._stream:track_end() then
        return true
    else
        self:_on_error(true)
        return false
    end
end

function M.player_can_buffer(player, data_len)
    return player.state.streambuf_free
            > (STREAM_COMMAND_SIZE + data_len)
end

function Player:buffer(data)
    self.state.streambuf_free =
        self.state.streambuf_free - STREAM_COMMAND_SIZE - string.len(data)
    if not self.has_error and self._stream:buffer(data) then
        return true
    else
        self:_on_error(true)
        return false
    end
end

function Player:flush(set_cookie)
    if not self.has_error and self._stream:flush(set_cookie) then
        return true
    else
        self:_on_error(true)
        return false
    end
end

function Player:start(data)
    self.state.streambuf_free =
            self.state.streambuf_free - STREAM_COMMAND_SIZE
    if not self.has_error and self._stream:start() then
        return true
    else
        self:_on_error(true)
        return false
    end
end

function Player:skip_ahead(interval_ms)
    self:call('skip_ahead', {interval=interval_ms})
end

function M.new(...)
    local o = {}
    setmetatable(o, Player)
    Player.__index = Player
    o:init(...)
    return o
end

return M
