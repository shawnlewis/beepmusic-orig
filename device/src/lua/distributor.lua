#!/usr/bin/env lua

-- TODO:
-- - centralize app_ubus_obj
-- - flush while joining
-- - acquire while joining

require 'ubus'
require 'uci'
require 'uloop'
local inspect = require 'inspect'

require 'beep_ubus'
require 'util'

require 'beep'
require 'stream'


local config = require 'config'
local flags = require 'flags'
local log = require 'log'
local beepcloud = require 'beepcloud'
local beep_player = require 'beep_player'
local replay_log = require 'replay_log'

local distributor_players = require 'distributor_players'
local distributor_history = require 'distributor_history'

require 'strict'

----- on startup

flags.init(arg)
log:init('distributor')

uloop.init()

local function remove_req(val)
    val = deepcopy(val)
    val.req = nil
    return val
end
replay_log.add_filter('channel_put', 'ubus_players', remove_req)
replay_log.add_filter('channel_put', 'ubus_play', remove_req)
replay_log.add_filter('channel_put', 'ubus_state', remove_req)
replay_log.add_filter('channel_put', 'ubus_acquire', remove_req)
replay_log.add_filter('channel_put', 'ubus_stream', function(val)
    val = deepcopy(val)
    val.req = nil
    if val.method == 'buffer' then
        val.msg.data_len = string.len(val.msg.data)
        val.msg.data = nil
    end
    return val
end)
replay_log.add_filter('channel_put', 'player_ready', function(val)
    if val then
        val = val.id
    end
    return val
end)

-- connect to ubus
local conn = beep_ubus_connect('distributor')

local local_device_id = config.device_get('device_id')
local global_volume = config.devel_get('global_volume')

--replay_log.enable_logging('/tmp/distrib_replay-' .. local_device_id .. '.txt')

local default_volume = 500
local vol_string = config.devel_get('default_volume')
if vol_string then
    default_volume = tonumber(vol_string)
end

----- end on startup

-- this is also declared in lib/audio/streambuf.h
local STREAMBUF_SIZE = 1 * 1024 * 1024
local MAX_EXTRA_BUFFER = 500 * 1024   -- enough for 10s of spotify audio

local TOKEN_MAX = 1000000
local TOKEN_INVALID = TOKEN_MAX + 1

local NIL = {} -- used to represent a nil value in a table

local app_ubus_obj


-- state that we report to controllers (filtered through the get_state
-- function)
local state = {
    audio_state = 'paused',
    play_state = 'stopped',

    track_info = nil,
    station = nil,
    track_elapsed_secs = nil,

    -- stores player volumes. We keep these as floats so that if the
    -- master volume is set to zero and then back up to some N, the
    -- individual volumes scale appropriately. In other words we want
    -- to always maintain the relative ratio between volume sliders, even
    -- when they are at integer zero.
    players = {},
    master_volume = default_volume,
    -- to enable clients to ignore their own master volume-changing events,
    -- we attach an arbitrary and opaque client_id string to the master
    -- volume.  clients should supply an id that is at least unique to
    -- the client instance.
    master_volume_client_id = '',
    local_volume = 0
}

local last_track_advance_time = 0
local RESTART_STATION_TIMEOUT = 20 * 60  -- 20 minute

local history
-- give it a second to make sure beepcloud is up.
uloop.timer(function()
    history = distributor_history.History(function()
        state.history = history:get_list()
        trigger_update('history_updated')
    end)
end, 1000)


local function play_history(index)
    local station = history:at(index)
    if not station or not station.app
            or not station.method or not station.args then
        play_station(DEFAULT_STATION)
    end
    local ubus_obj = 'beep.app.' .. station.app
    ubus_call(conn, ubus_obj, station.method, station.args,
            function()
                log:info('Replay request complete.')
            end)
end

local function get_state()
    local s = deepcopy(state)
    for id, player_state in pairs(s.players) do
        player_state.volume = math.floor(player_state.volume)
    end

    -- Always include track_info and station, even if empty
    if s['track_info'] == nil then
        s['track_info'] = {}
    end

    if s['station'] == nil then
        s['station'] = {}
    end

    s.master_volume = math.floor(s.master_volume)
    s.local_volume = math.floor(s.local_volume)
    return s
end

local function set_app_ubus_obj(app_ubus_obj_in)
    if app_ubus_obj_in ~= app_ubus_obj then
        app_ubus_obj = app_ubus_obj_in
        beepcloud.set_group('current_app', app_ubus_obj)
        local app_name = app_ubus_obj:gsub('beep%.app%.', '')
        state.station = {
            app = app_name,
        }
        trigger_update('station_changed')
    end
end

local function blacklisted_event_type(event_type)
    if event_type == 'progress' then
        return true
    end

    return false
end

function trigger_update(event_name, event_data)
    if not blacklisted_event_type(event_name) then
        log:debug('trigger_update called with ' .. event_name)
    end
    beep_send_state(
            conn, 'distributor', event_name, event_data, get_state())
end

-- distributor's audio state

-- we won't send a resume til player1 has buffered at least this much
-- pcm data
local AUDIO_START_WATERMARK_DEFAULT = 40 * 1024  -- 100kB approx == .25s

-- if we have an underrun we switch to the slow start watermark
local AUDIO_START_WATERMARK_SLOW = 1500 * 1024  -- 1.5MB approx == 8s

-- we revert back to the default watermark after this much time in seconds
local SLOW_AUDIO_START_TIMEOUT = 30  -- seconds

local audio_start_watermark = AUDIO_START_WATERMARK_DEFAULT
local audio_start_watermark_revert_timer = nil

local AUDIO_LOW_WATERMARK = 10 * 1024


DEFAULT_STATION = {
    app = 'beep.app.webradio',
    method = 'play_station',
    args = {
        url = 'http://ice.somafm.com/groovesalad',
        name = 'SomaFM: Groove Salad',
        image_url = 'http://somafm.com/logos/512/groovesalad512.png'
    }
}

local track_volume_scalar = 1000

local GAIN_MAX = math.pow(2, 16)
local volume_to_gain_table = {}
local gain_to_volume_table = {}
for vol = 1000, 0, -1 do
    -- iterate backwards so we get values for 0 in each table.
    local gain = math.floor(GAIN_MAX * vol * vol * vol / (1000 * 1000 * 1000))
    volume_to_gain_table[vol] = gain
    gain_to_volume_table[gain] = vol
end
function gain_to_volume(gain)
    gain = math.floor(gain)
    if gain > GAIN_MAX then
        gain = GAIN_MAX
    elseif gain < 0 then
        gain = 0
    end
    local volume = -1
    if gain_to_volume_table[gain] then
        volume = gain_to_volume_table[gain]
    else
        -- find nearest below
        for i = gain, GAIN_MAX do
            if gain_to_volume_table[i] then
                volume = gain_to_volume_table[i]
                break
            end
        end
    end
    if volume == -1 then
        log:error('Should never get here!')
        os.exit(1)
    end
    volume = math.floor(volume * 1000 / track_volume_scalar)
    return volume
end
function volume_to_gain(volume)
    volume = math.floor(volume * track_volume_scalar / 1000)
    if not volume_to_gain_table[volume] then
        volume = 0
    end
    local gain = volume_to_gain_table[volume]
    return gain
end

local volume_players = {}
local function volume_available_players()
    return volume_players
end

function update_player_state_volume(player_id, name, volume, no_trigger_update)
    if not state.players[player_id] then
        state.players[player_id] = {}
        state.players[player_id].is_muted = false
    end
    state.players[player_id].volume = volume
    state.players[player_id].name = name
    if player_id == local_device_id then
        state.local_volume = volume
    end

    if global_volume and (not no_trigger_update) then
        trigger_update('volume_changed', get_state().players)
    else
        local max_vol = 0
        for id, player in pairs(state.players) do
            if player.volume > max_vol then
                max_vol = player.volume
            end
        end
        state.master_volume = max_vol

        if state.master_volume == 0 then
            state.master_volume = 0.1
        end

        if not no_trigger_update then
            -- no_trigger_update is only set to true from set_master_volume
            -- i.e., client_id should be reverted to '' (none) when
            -- no_trigger_update is false
            state.master_volume_client_id = ''
            trigger_update('volume_changed', get_state().players)
        end
    end
end

-- caller is responsible for ensuring player_id is valid.
function set_player_volume(player_id, volume, no_trigger_update)
    if volume < 0 then
        volume = 0
    elseif volume > 1000 then
        volume = 1000
    end

    local volume_players = volume_available_players()
    local player = volume_players[player_id]
    -- we immediately tell the client the volume has changed since the
    -- below command will either succeed or the device will be marked
    -- dead.
    update_player_state_volume(player_id, player.name, volume, no_trigger_update)

    if state.players[player_id].is_muted then
        volume = 0
    end
    volume_players[player_id]:call(
            'set_volume', {gain= volume_to_gain(volume)})
end

function set_player_is_muted(player_id, is_muted)
    log:debug('IS_MUTED: %s %s', player_id, is_muted)
    local player_state = state.players[player_id]
    if player_state then
        player_state.is_muted = is_muted
        set_player_volume(player_id, player_state.volume)
    end
end

-- caller is responsible for ensuring player_id is valid.
function adjust_player_volume(player_id, volume_delta)
    if not state.players[player_id] then
        return
    end
    local volume = state.players[player_id].volume + volume_delta
    if volume < 0 then
        volume = 0
    elseif volume > 1000 then
        volume = 1000
    end

    if global_volume then
        set_master_volume(volume)
    else
        local volume_players = volume_available_players()
        local player = volume_players[player_id]
        update_player_state_volume(player_id, player.name, volume)
        if state.players[player_id].is_muted then
            volume = 0
        end
        volume_players[player_id]:call(
                'set_volume', {gain= volume_to_gain(volume)})
    end
end

function set_master_volume(volume, client_id)
    -- This check ensures we don't get a divide by zero, by never allowing
    -- the master volume to be zero (so the ratio calculation below will
    -- always succeed).
    if global_volume then
        for id, player_state in pairs(state.players) do
            set_player_volume(id, volume, true)
        end
    else
        if volume == 0 then
            volume = .1
        end
        local volume_ratio = volume / state.master_volume
        log:info('VOLUME RATIO: %s', volume_ratio)
        for id, player_state in pairs(state.players) do
            if player_state.volume < 0.1 then
                player_state.volume = 0.1
            end
            local new_player_volume = volume_ratio * player_state.volume
            set_player_volume(id, new_player_volume, true)
        end
    end

    state.master_volume_client_id = client_id
    trigger_update('volume_changed')
end

-- this introduces a bug. we use playnet's gain to store individual player
-- volumes, but now we're modifying that gain based on the track volume.
-- so if a playnet has it's gain set based on a track volume scalar, and then
-- a new master takes over that playnet it will compute an incorrect volume
-- for that player.
--
-- TODO: pass track volume scalar down into playnet and let it handle it.
function set_track_volume_scalar(volume)
    track_volume_scalar = volume

    -- trigger update of player volumes
    set_master_volume(state.master_volume)
end

function all_players_call(players, method, args)
    for id, player in pairs(players) do
        player:call(method, args)
    end
end

function all_players_resume(players, delta_ms)
    local now_high, now_low = beep.beep_millis()
    local h, l = beep.beep_millis_add(now_high, now_low, delta_ms)
    all_players_call(players, 'resume', {timeh=h, timel=l}, nil, nil)
end

function play_station(station)
    ubus_call(conn, station.app, station.method, station.args)
end


function player_stop(player, cookie, stopped_ch, error_ch)
    async.go(function()
        player:call('stop', {set_cookie=cookie}, function(result)
            if result then
                stopped_ch:put(player)
            else
                error_ch:put(player)
            end
        end)
    end)
end

-- blocks until players stopped
function stop_players(players, cookie)
    local stopped_ch = async.Channel(async.Buffer())
    local error_ch = async.Channel(async.Buffer())
    for _, player in pairs(players) do
        player_stop(player, cookie, stopped_ch, error_ch)
    end

    local failed_players = {}
    local num_players = table_length(players)
    for i = 1, num_players do
        local event_ch = async.alts({stopped_ch, error_ch})
        local player = event_ch:take()
        if event_ch == error_ch then
            failed_players[player.id] = player
        else
            if not player:start() then
                failed_players[player.id] = player
            end
        end
    end

    return failed_players
end

-- there are really two threads with their own timescales:
--    stream thread and play thread
--

--
-- allow acquire during any of these, break out to top if so
--
-- how to handle flush?
--     bug is when we get a flush after track_started but before track_begin?
--     need to look at what spotify does here... they should be the ones tracking it
--
--
-- stream thread:
-- acquire then:
--     can_track_begin or track_begin
--     can_buffer or buffer or set_track_info
--     track_end
--
-- app thread:
--     waits for changes to app_ubus_obj and gets app events
--     (play/pause/set_station/skip/prev)
--
-- each acquire could be it's own thread...
--
-- how does stream joining happen?
--
-- add_player process:
--     we'll have a new_players channel
--     we use alts to select it in specific places during stream thread
--     start a separate add-player thread that does the joining, although we can
--     abort if we get an acquire during this... but how do we know if we get
--     an acquire? We could handle all stream events and just return WOULD_BLOCK
--     but what about if we get a flush?


--   - just send via a channel?
local THREAD_RESULT_OK = 1
local THREAD_RESULT_ACQUIRE = 2
local THREAD_RESULT_FLUSH = 3
local THREAD_RESULT_TRACK_END = 4
local THREAD_RESULT_TRACK_BEGIN = 5
local THREAD_RESULT_REBUFFER_OK = 6

STREAM_EVENT_WAITING_FOR_ACQUIRE = 7
STREAM_EVENT_ACQUIRE_STARTED = 0
STREAM_EVENT_ACQUIRE_DONE = 1
STREAM_EVENT_TRACK_BEGIN = 2
STREAM_EVENT_TRACK_END = 3
STREAM_EVENT_FLUSH = 4
STREAM_EVENT_BUFFER = 5
STREAM_EVENT_REBUFFER = 6


function handle_stream_command(
        owner_token, begin_immediately, need_rebuffer,
        players, players_ch, event_ch, acquire_ch,
        ubus_stream_command_ch, player_states_ch,
        stream_event_ch)  -- outputs
    local stream_command = event_ch:take()

    --log:debug('handle_stream_command stream_command: %s', stream_command.method)

    if stream_command.method == 'acquire' then
        return THREAD_RESULT_ACQUIRE, nil, stream_command

    elseif stream_command.msg.token ~= 1234567
            and stream_command.msg.token ~= owner_token then
        beep_reply(conn, stream_command.req,
                beep_error('Invalid token',
                           BEEP_UBUS_ERROR_DISTRIBUTOR_INVALID_TOKEN))
        return THREAD_RESULT_OK

    elseif stream_command.method == 'can_track_begin' then
        if need_rebuffer then
            beep_reply2(stream_command.req, beep_error(
                    'track_begin would block',
                    BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK))
            return THREAD_RESULT_REBUFFER_OK
        end
        if begin_immediately then
            beep_reply2(stream_command.req, beep_success())
        elseif all_vals(players, beep_player.player_can_track_begin) then
            beep_reply2(stream_command.req, beep_success())
        else
            beep_reply2(stream_command.req, beep_error(
                    'track_begin would block',
                    BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK))
        end
        return THREAD_RESULT_OK

    elseif stream_command.method == 'track_begin' then
        stream_event_ch:put({
            event = STREAM_EVENT_TRACK_BEGIN,
            content_length = stream_command.msg.content_length,
            track_info = stream_command.msg.track_info,
            audio_type = stream_command.msg.audio_type
        })
        local new_players = nil
        if not begin_immediately then
            while true do
                local all_can_begin = all_vals(players,
                        beep_player.player_can_track_begin)
                if all_can_begin then
                    break
                end
                local event_ch = async.alts({players_ch, acquire_ch, player_states_ch})
                if event_ch == players_ch then
                    new_players = players_ch:take()
                    if table_length(new_players) == 0 then
                        beep_reply2(stream_command.req, beep_success())
                        return THREAD_RESULT_OK, new_players
                    end
                elseif event_ch == acquire_ch then
                    local acquire_command = acquire_ch:take()

                    -- We can tell the requester that the track_begin was
                    -- successful. Their next track_begin will fail due to a
                    -- token change.
                    beep_reply2(stream_command.req, beep_success())
                    return THREAD_RESULT_ACQUIRE, new_players, acquire_command
                else  -- player_states_ch
                    player_states_ch:take()
                end
            end
        end
        for id, player in pairs(players) do
            player:track_begin(stream_command.msg.audio_type)
        end

        beep_reply2(stream_command.req, beep_success())

        return THREAD_RESULT_TRACK_BEGIN, new_players

    elseif stream_command.method == 'can_buffer' then
        if need_rebuffer then
            beep_reply2(stream_command.req, beep_error(
                    'track_begin would block',
                    BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK))
            return THREAD_RESULT_REBUFFER_OK
        end
        if all_vals(players,
                    function(p)
                        return beep_player.player_can_buffer(
                            p, stream_command.msg.data_len)
                    end) then
            beep_reply2(stream_command.req, beep_success())
        else
            beep_reply2(stream_command.req, beep_error(
                    'buffer would block',
                    BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK))
        end
        return THREAD_RESULT_OK

    elseif stream_command.method == 'buffer' then
        local size = string.len(stream_command.msg.data)
        local new_players = nil
        while true do
            local all_can_begin = all_vals(players,
                function(p)
                    return beep_player.player_can_buffer(p, size)
                end)
            if all_can_begin then
                break
            end

            local event_ch = async.alts({players_ch, acquire_ch, player_states_ch})
            if event_ch == players_ch then
                new_players = players_ch:take()
                if table_length(new_players) == 0 then
                    beep_reply2(stream_command.req, beep_success())
                    return THREAD_RESULT_OK, new_players
                end

            elseif event_ch == acquire_ch then
                local acquire_command = acquire_ch:take()

                -- We can tell the requester that the buffer was successful.
                -- Their next buffer will fail due to a token change.
                beep_reply2(stream_command.req, beep_success())
                return THREAD_RESULT_ACQUIRE, new_players, acquire_command
            else  -- player_states_ch
                player_states_ch:take()
            end
        end
        for id, player in pairs(players) do
            player:buffer(stream_command.msg.data)
        end
        stream_event_ch:put({
            event = STREAM_EVENT_BUFFER,
            data = stream_command.msg.data,
            size = size})

        -- TODO notify play thread so that it can autostart

        beep_reply2(stream_command.req, beep_success())

        return THREAD_RESULT_OK, new_players

    elseif stream_command.method == 'flush' then

        beep_reply2(stream_command.req, beep_success())

        return THREAD_RESULT_FLUSH

    elseif stream_command.method == 'track_end' then
        for id, player in pairs(players) do
            player:track_end()
        end

        stream_event_ch:put({event = STREAM_EVENT_TRACK_END})
        beep_reply2(stream_command.req, beep_success())

        return THREAD_RESULT_TRACK_END

    else
        log:warn('Got out of sequence stream command: %s', stream_command)
        beep_reply2(stream_command.req, beep_error(
                'Stream command sent out of sequence'))

        return THREAD_RESULT_OK

    end
end

function stream_thread(
        players_ch,
        acquire_ch,
        ubus_stream_command_ch,
        player_states_ch,
        debug_ch,
        stream_event_ch, cookie_ch, streaming_players_ch) -- outputs
return async.go(function()
    local owner_token = nil
    local cookie = 0

    local players = {}

    local stream_command = {}

    local function do_debug()
        local our_locals = locals(3)
        local f = io.open('/tmp/distributor_stream_thread.txt', 'w')
        f:write('STREAM_THREAD LOCALS: ' .. inspect(our_locals) .. '\n')
        f:close()
    end

    -- stream loop
    while true do
        -- at the top of this loop, either we have zero players, xor we have
        -- an acquire command ready (stream_command)

        if table_length(players) == 0 then
            -- This happens when we've bailed on buffering below because we ran
            -- out of players.
            -- Wait for players to be added again.
            stream_event_ch:put({event = STREAM_EVENT_WAITING_FOR_ACQUIRE})
            while true do
                local event_ch = async.alts(
                        {players_ch, acquire_ch, ubus_stream_command_ch, debug_ch})
                if event_ch == debug_ch then
                    debug_ch:take()
                    do_debug()
                elseif event_ch == players_ch then
                    players = event_ch:take()
                elseif event_ch == ubus_stream_command_ch then
                    local command = ubus_stream_command_ch:take()
                    beep_reply(conn, command.req, beep_error(
                            'Stream command invalid before acquire'))
                else  -- acquire_ch
                    stream_command = event_ch:take()
                    if table_length(players) == 0 then
                        beep_reply(conn, stream_command.req, beep_error(
                                'Can\'t acquire when distributor has no players'))
                    else
                        break
                    end
                end
            end
        end
        if stream_command.method ~= 'acquire' then
            log:error('Error, expected stream_command acquire at top of '
                    .. 'stream_thread but got: %s. Exiting...',
                    stream_command.method)
            os.exit(1)
        end
        log:info('DOING ACQUIRE')

        streaming_players_ch:put(players)

        -- stream_command is always an acquire at this point

        cookie = cookie + 1
        cookie_ch:put(cookie)

        set_track_volume_scalar(1000)

        stream_event_ch:put({
            event = STREAM_EVENT_ACQUIRE_STARTED,
            app_ubus_obj = stream_command.msg.app_ubus_obj})

        -- stop and start all players
        -- TODO: add new player if we get one while doing the stops.
        stop_players(players, cookie)

        log:debug('DONE STOPPING')
        stream_event_ch:put({event=STREAM_EVENT_ACQUIRE_DONE})

        owner_token = math.random(TOKEN_MAX)
        beep_reply2(stream_command.req, beep_success({token=owner_token}))

        local can_definitely_track_begin = true

        -- track buffering loop
        -- We can break in this loop for two reasons:
        --     1) we got a new acquire
        --     2) we ran out of players

        local need_rebuffer = false
        local rebuffer_players = {}
        local rebuffer_after_track_end = false
        while true do
            local do_rebuffer = false
            local event_ch = async.alts(
                    {players_ch, acquire_ch, ubus_stream_command_ch, debug_ch})
            if event_ch == debug_ch then
                debug_ch:take()
                do_debug()
            elseif event_ch == players_ch then
                local new_players = players_ch:take()
                if table_length(new_players) == 0 then
                    players = {}
                    break
                end

                if rebuffer_is_needed(players, new_players) then
                    log:debug('Rebuffer needed, waiting for opportune moment')
                    need_rebuffer = true
                    rebuffer_players = new_players

                    if rebuffer_after_track_end then
                        log:debug('Doing rebuffer after track end')
                        do_rebuffer = true
                    end
                else
                    need_rebuffer = false
                    players = new_players
                    streaming_players_ch:put(new_players)
                end
            else
                local result, new_players, result_arg = handle_stream_command(
                    owner_token,
                    can_definitely_track_begin,
                    need_rebuffer,
                    players,
                    players_ch,
                    event_ch,
                    acquire_ch,
                    ubus_stream_command_ch,
                    player_states_ch,
                    stream_event_ch)
                if new_players then
                    if table_length(new_players) == 0 then
                        players = {}
                        break
                    end
                    if rebuffer_is_needed(players, new_players) then
                        log:debug('Rebuffer needed, waiting for opportune moment')
                        need_rebuffer = true
                        rebuffer_players = new_players
                    else
                        need_rebuffer = false
                        players = new_players
                        streaming_players_ch:put(new_players)
                    end
                end
                -- if we get a track end we can rebuffer now
                if result == THREAD_RESULT_TRACK_END then
                    rebuffer_after_track_end = true
                    if need_rebuffer then
                        result = THREAD_RESULT_REBUFFER_OK
                    end
                else
                    rebuffer_after_track_end = false
                end

                if result == THREAD_RESULT_REBUFFER_OK then
                    do_rebuffer = true
                elseif result == THREAD_RESULT_TRACK_BEGIN then
                    can_definitely_track_begin = false
                elseif result == THREAD_RESULT_ACQUIRE then
                    stream_command = result_arg
                    if need_rebuffer then
                        players = rebuffer_players
                    end
                    break
                elseif result == THREAD_RESULT_FLUSH then
                    cookie = cookie + 1
                    cookie_ch:put(cookie)
                    stream_event_ch:put({event = STREAM_EVENT_FLUSH})
                    for id, player in pairs(players) do
                        player:flush(cookie)
                    end
                end
            end
            if do_rebuffer then
                log:debug('stream_thread: Doing rebuffer')
                cookie = cookie + 1

                players = rebuffer_players
                streaming_players_ch:put(players)

                -- This REBUFFER event is used to make sure the play thread
                -- doesn't think the last event was TRACK_ENDED (which
                -- would cause it to send a track_end to the owning app
                -- which some apps use to start the next track).
                stream_event_ch:put({event = STREAM_EVENT_REBUFFER})
                rebuffer_request_ch:put({players, cookie})

                local listen_chs = {rebuffer_done_ch, ubus_stream_command_ch, debug_ch}
                while true do
                    local event_ch = async.alts(listen_chs)
                    if event_ch == debug_ch then
                        debug_ch:take()
                        do_debug()
                    elseif event_ch == rebuffer_done_ch then
                        log:debug('GOT REBUFFER DONE')
                        rebuffer_done_ch:take()
                        break
                    else
                        local stream_command = ubus_stream_command_ch:take_async()
                        if stream_command.method == 'can_buffer'
                                or stream_command.method == 'can_track_begin' then
                            log:debug('REPLYING WOULD BLOCK DURING REBUFFER!!!!!')
                            beep_reply2(stream_command.req, beep_error(
                                    'would block during rebuffer',
                                    BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK))
                        else
                            log:debug('GOT A DIFFERENT STREAM_COMMAND DURING REBUFFER')
                            -- Stop replying would block, so we'll actually block
                            -- can_buffer and can_track_begin if this happens.
                            -- TODO: handle flush/acquire during rebuffer

                            -- We put the command back so we can handle it later
                            ubus_stream_command_ch:put(stream_command)
                            listen_chs = {rebuffer_done_ch, debug_ch}
                        end
                    end
                end


                if rebuffer_after_track_end then
                    -- notify the play thread that it can send the track end
                    -- event when audio runs out.
                    stream_event_ch:put({event = STREAM_EVENT_TRACK_END})
                end

                need_rebuffer = false
                rebuffer_players = {}
                log:debug('stream_thread: Rebuffer done')
            end
        end
    end
end)
end

PLAY_STATE_PAUSED = 0
PLAY_STATE_AUTOSTART = 1
PLAY_STATE_STARTING = 2
PLAY_STATE_PLAYING = 3

PLAY_EVENT_DURATION_CHANGE = 0
PLAY_EVENT_TRACK_STARTED = 1
PLAY_EVENT_TRACK_ENDED = 2

-- output: play_state_ch
function play_thread(players_ch, stream_event_ch, player_states_ch, play_command_ch,
        debug_ch,
        play_state_ch, play_event_ch) -- output
return async.go(function()
    local audio_state = PLAY_STATE_PAUSED
    local players = {}
    local have_sent_duration = false  -- true if we've sent a duration for this track
    local track_content_length = 0
    local most_recent_stream_event = nil

    local num_tracks_started = 0
    local pending_track_info = nil
    local did_send_audio_ended = false

    local missed_cookie_count = 0
    local resume_timer = nil

    local function update_audio_state(state)
        audio_state = state
        if audio_state == PLAY_STATE_PLAYING then
            resume_timer = nil
        end
        play_state_ch:put(audio_state)
    end

    local function send_duration_change(duration)
        play_event_ch:put({event=PLAY_EVENT_DURATION_CHANGE, duration=duration})
        have_sent_duration = true
    end

    while true do
        local channels = {players_ch, player_states_ch, play_command_ch,
                stream_event_ch, debug_ch}
        if resume_timer then
            table.insert(channels, resume_timer)
        end
        local event_ch = async.alts(channels)
        if event_ch == debug_ch then
            debug_ch:take()
            local our_locals = locals()
            local f = io.open('/tmp/distributor_play_thread.txt', 'w')
            f:write('PLAY_THREAD LOCALS: ' .. inspect(our_locals) .. '\n')
            f:close()
        elseif event_ch == players_ch then
            players = players_ch:take()
        elseif event_ch == play_command_ch then
            local play_command = play_command_ch:take()
            log:info('PLAY THREAD COMMAND: %s', play_command.method)
            if play_command.method == 'pause' then
                if audio_state ~= PLAY_STATE_PAUSED then
                    update_audio_state(PLAY_STATE_PAUSED)
                    all_players_call(players, 'pause')
                end
            elseif play_command.method == 'resume' then
                if audio_state == PLAY_STATE_PAUSED then
                    update_audio_state(PLAY_STATE_AUTOSTART)
                end
            elseif play_command.method == 'app_pause' then
                if audio_state ~= PLAY_STATE_PAUSED then
                    local handle_app_pause_result = function(result, errors)
                        if not result and errors.ubus_error_code
                                == UBUS_STATUS_METHOD_NOT_FOUND then
                            play_command_ch:put({
                                method = 'pause',
                                req = nil,
                                msg = nil})
                        end
                    end
                    if app_ubus_obj then
                        ubus_call(conn, app_ubus_obj, 'pause', nil,
                                handle_app_pause_result, {method_not_found_ok=true})
                    else
                        play_command_ch:put({
                            method = 'pause',
                            req = nil,
                            msg = nil})
                    end
                end
            elseif play_command.method == 'app_resume' then
                -- try to resume the current app
                -- if the app does nothing within a timeout then play webradio
                if audio_state == PLAY_STATE_PAUSED then
                    if not app_ubus_obj then
                        log:debug('No current app, playing first history item.')
                        play_history(1)
                    elseif app_ubus_obj == 'beep.app.spotify' then
                        log:debug('Resuming spotify')
                        -- only spotify handles resume for now.
                        ubus_call(conn, app_ubus_obj, 'resume', nil,
                                nil,
                                {method_not_found_ok=true})
                    elseif most_recent_stream_event.event
                            == STREAM_EVENT_WAITING_FOR_ACQUIRE then
                        -- we've just become master
                        log:debug('Playing first history item.')
                        play_history(1)
                    elseif os.time() - last_track_advance_time >
                            RESTART_STATION_TIMEOUT then
                        log:debug('Last audio played time exceeded ' ..
                                  'RESTART_STATION_TIMEOUT. ' ..
                                  'Playing first history item.')
                        play_history(1)
                    else
                        log:debug('Resuming buffer.')
                        play_command_ch:put({
                            method = 'resume',
                            req = nil,
                            msg = nil})
                    end
                end
            end
        elseif event_ch == resume_timer then
            log:debug('No audio resumed within timeout, playing first'
                    .. 'history item')
            play_history(1)
            resume_timer = nil
        elseif event_ch == stream_event_ch then
            local stream_event = stream_event_ch:take()
            --log:debug('play_thread: got stream_event: %s', stream_event)
            most_recent_stream_event = stream_event

            if stream_event.event == STREAM_EVENT_ACQUIRE_DONE then
                if audio_state == PLAY_STATE_STARTING
                        or audio_state == PLAY_STATE_PLAYING then
                    update_audio_state(PLAY_STATE_AUTOSTART)
                end
                track_content_length = 0
                num_tracks_started = 0
                pending_track_info = nil
            elseif stream_event.event == STREAM_EVENT_TRACK_BEGIN then
                pending_track_info = stream_event.track_info
                track_content_length = stream_event.content_length
                have_sent_duration = false
            elseif stream_event.event == STREAM_EVENT_TRACK_END then
                did_send_audio_ended = false
            elseif stream_event.event == STREAM_EVENT_FLUSH then
                all_players_call(players, 'pause')
                update_audio_state(PLAY_STATE_AUTOSTART)
            end
        elseif event_ch == player_states_ch then
            local cookie_matched_ids, non_matched_ids = unpack(player_states_ch:take())
            local _, player1 = next(players)

            if not is_subset_of(players, cookie_matched_ids) then
                missed_cookie_count = missed_cookie_count + 1
                if missed_cookie_count == 20 then
                    log:error('missed_cookie_count reached 20, programming error?')
                end
            elseif player1 then
                missed_cookie_count = 0


                ----- audio_state management

                local all_players_ready = all_vals(players, function(player)
                    return player.state.output_used >= audio_start_watermark
                end)
                if audio_state == PLAY_STATE_AUTOSTART and all_players_ready then
                    log:info('Autostarting players now. player1 output_used: %s, '
                            .. 'watermark: %s',
                            player1.state.output_used, audio_start_watermark)
                    update_audio_state(PLAY_STATE_STARTING)

                    -- argument should be > wifi latency + time it takes playnets
                    -- to handle command.
                    all_players_resume(players, 100)
                end

                -- the STARTING state waits for advancing written_track_time before
                -- entering the PLAYING state (wherein we can detect underruns)
                local all_players_advanced = all_vals(players, function(player)
                    return player.prev_state.written_track_time
                            and player.state.written_track_time
                                > player.prev_state.written_track_time
                end)
                if audio_state == PLAY_STATE_STARTING and all_players_advanced then
                    log:debug('Moving to PLAY_STATE_PLAYING')
                    update_audio_state(PLAY_STATE_PLAYING)

                    -- audio is advancing but we haven't received a bitrate, assume
                    -- that we never will and send a duration of 0.
                    if not have_sent_duration then
                        send_duration_change(0)
                    end
                end

                -- TODO: move to state_thread
                ----- send duration if we have it.

                if (not have_sent_duration) and player1.state.bitrate ~= 0 then
                    local duration = track_content_length * 8
                            / (player1.state.bitrate * 1024)
                    send_duration_change(math.ceil(duration))
                end

                -- TODO: move to state_thread
                ----- send track_info if a new track_started

                if player1.state.num_tracks_started > num_tracks_started then
                    log:debug('Track started increment for player: %s', player1.id)
                    assert(player1.state.num_tracks_started == num_tracks_started + 1)
                    num_tracks_started = num_tracks_started + 1

                    -- This is private user info, don't log in production
                    --log:info('PENDING TRACK_INFO: %s', pending_track_info)
                    play_event_ch:put({
                            event = PLAY_EVENT_TRACK_STARTED,
                            track_info = pending_track_info})
                    pending_track_info = nil
                end

                ----- underflow logic

                local any_players_underrun = any_vals(players, function(player)
                    return player.state.output_used <= AUDIO_LOW_WATERMARK
                end)
                if most_recent_stream_event.event ~= STREAM_EVENT_TRACK_END then
                    -- look for underruns and pause so we can catch back up.
                    if (audio_state == PLAY_STATE_PLAYING
                                or audio_state == PLAY_STATE_STARTING)
                            and any_players_underrun then
                        log:warn('Hit low audio watermark, pausing for buffering.')

                        -- bad network conditions are a likely culprit for the underrun,
                        -- so don't start audio til we have a bigger buffer.
                        audio_start_watermark = AUDIO_START_WATERMARK_SLOW

                        -- revert to a small audio start buffer after some time has
                        -- passed
                        if audio_start_watermark_revert_timer then
                            audio_start_watermark_revert_timer:cancel()
                        end
                        audio_start_watermark_revert_timer = uloop.timer(function()
                            log:info('Setting audio_start_watermark back to default')
                            audio_start_watermark = AUDIO_START_WATERMARK_DEFAULT
                            audio_start_watermark_revert_timer = nil
                        end, SLOW_AUDIO_START_TIMEOUT * 1000)

                        -- pause and move to AUTOSTART
                        update_audio_state(PLAY_STATE_AUTOSTART)
                        all_players_call(players, 'pause')
                    end
                else
                    if player1.state.output_used == 0 and not did_send_audio_ended then
                        did_send_audio_ended = true;
                        log:info('Reached end of track, with no other tracks queued.')
                        play_event_ch:put({event = PLAY_EVENT_TRACK_ENDED})

                        -- we'll autostart the next track, but don't send a notification
                        -- yet.
                        audio_state = PLAY_STATE_AUTOSTART
                        all_players_call(players, 'pause')

                        -- If we don't get a new TRACK_START or ACQUIRE within 10 seconds
                        -- we go to paused.
                        -- TODO: if we go to paused here the user shouldn't be able
                        --      to resume, since there is nothing buffered, maybe
                        --      blow away app_ubus_obj since the app is no longer
                        --      streaming?
                        uloop.timer(function()
                            if most_recent_stream_event.event
                                    == STREAM_EVENT_TRACK_END then
                                update_audio_state(PLAY_STATE_PAUSED)
                            end
                        end, 10000)
                    end
                end
            end
        end
    end
end)
end

function sync_thread(players_ch, player_states_ch, play_state_ch)
return async.go(function()
    local players = {}
    local play_state = -1
    local missed_cookie_count = 0

    while true do
        local event_ch = async.alts({players_ch, player_states_ch, play_state_ch})
        if event_ch == players_ch then
            players = players_ch:take()
        elseif event_ch == play_state_ch then
            play_state = play_state_ch:take()
        elseif event_ch == player_states_ch then
            local cookie_matched_ids, non_matched_ids = unpack(player_states_ch:take())

            local _, player1 = next(players)
            if not player1 then
                -- no nothing
            elseif not is_subset_of(players, cookie_matched_ids) then
                missed_cookie_count = missed_cookie_count + 1
                if missed_cookie_count == 20 then
                    log:error('Sync thread missed_cookie_count reached 20. '
                            .. 'Programming error?')
                end
            else
                missed_cookie_count = 0

                local did_skip
                if play_state == PLAY_STATE_PLAYING
                        and all_vals(players, beep_player.is_syncable) then
                    local min_time = player1.apparent_start_time
                    for id, player in pairs(players) do
                        if player.apparent_start_time < min_time then
                            min_time = player.apparent_start_time
                        end
                    end
                    for id, player in pairs(players) do
                        local delta = 1000 * (player.apparent_start_time - min_time)

                        -- TODO: This should never be larger than a handful of samples
                        if delta > 10000000 then
                            log:error('Invalid skip delta: %d.', delta)
                        elseif delta >= 3000 then
                            -- set can_try_sync to false here in case we get recursively
                            -- called while the skip is outstanding.
                            log:info('doing skip for player %s, delta: %d', id, delta)

                            -- Tweak the 1.0 below to only skip part of the way to the
                            -- target time. Before we were only skipping 7/8 because
                            -- it seemed like we were going too far, possiblity due
                            -- to instability of using an average integers
                            -- (sync_played_time is currently reported as ms rather than
                            -- us).
                            player:skip_ahead(math.floor(delta * 1.0))
                            did_skip = true
                        elseif delta < 0 then
                            log:error('Tried to skip backwards, this shouldn\'t happen.')
                        end
                    end
                elseif play_state == PLAY_STATE_PAUSED then
                    local max_time = player1.state.written_track_time
                    for id, player in pairs(players) do
                        if player.state.written_track_time > max_time then
                            max_time = player.state.written_track_time
                        end
                    end
                    for id, player in pairs(players) do
                        local delta = 1000 * (max_time - player.state.written_track_time)

                        -- TODO: This can actually be fairly large due to network
                        -- latency
                        if delta > 10000000 then
                            log:error('Invalid paused skip delta: %d.', delta)
                        elseif delta > 0 then
                            log:info('doing paused skip for player %s, delta: %d',
                                    id, delta)
                            player:skip_ahead(delta)
                            did_skip = true
                        elseif delta < 0 then
                            log:error('Tried to skip backwards, this shouldn\'t happen.')
                        end
                    end
                end
                if did_skip then
                    -- TODO: actually ensure the skip happened instead of just
                    -- providing a timeout. If network latency is greater than the
                    -- timeout we could cascade skips.
                    async.timeout(1000, 'sync_thread_timeout'):take()
                    log:info('resetting sync states')
                    for id, player in pairs(players) do
                        player:reset_sync_state()
                        player:log_state()
                    end
                end
            end
        end
    end
end)
end

function buffer_thread(
        players_ch, player_states_ch, stream_event_ch,
        rebuffer_request_ch,
        streaming_players_ch, cookie_ch, rebuffer_done_ch)  -- outputs
return async.go(function()
    local players = {}

    local track_begin = nil
    local buffered_amount = 0
    local buffers = {}
    local headers = {}
    local track_end = nil
    local bitrate = 0
    local save_header = false
    local missed_cookie_count = 0

    local function header_len(headers)
        local size = 0
        for _, buf in ipairs(headers) do
            size = size + buf.size
        end
        return size
    end

    while true do
        local event_ch = async.alts({
                players_ch, player_states_ch, stream_event_ch, rebuffer_request_ch})
        if event_ch == players_ch then
            players = players_ch:take()
        elseif event_ch == player_states_ch then
            local cookie_matched_ids = unpack(player_states_ch:take())
            local _, player1 = next(players)

            if not is_subset_of(players, cookie_matched_ids) then
                missed_cookie_count = missed_cookie_count + 1
                if missed_cookie_count == 20 then
                    log:error('Buffer thread missed_cookie_count reached 20. '
                            .. 'Programming error?')
                end
            elseif player1 then
                missed_cookie_count = 0

                -- HACK: this is the Spotify bitrate
                if track_begin and track_begin.audio_type == 'o' then
                    bitrate = 320
                else
                    bitrate = player1.state.bitrate
                end
                local player_amount = STREAMBUF_SIZE - player1.state.streambuf_free

                -- Add in the amount used in the uncompressed buffer

                -- There is a factor of 8 for the uncompressed buffer because we store
                -- each sample as 32-bit, and always store in stereo
                -- TODO: don't depend on 44.1kHz
                local uncompressed_millis = math.floor(
                        player1.state.output_used * 1000 / (44100 * 8))
                player_amount = player_amount + uncompressed_millis * bitrate * 1024
                        / (1000 * 8)

                -- protect from running away with ram if for example we have an
                -- incorrect bitrate
                if player_amount > STREAMBUF_SIZE + MAX_EXTRA_BUFFER then
                    player_amount = STREAMBUF_SIZE + MAX_EXTRA_BUFFER
                end

                while buffered_amount > player_amount do
                    local popped = table.remove(buffers, 1)
                    if popped then
                        buffered_amount = buffered_amount - popped.size
                    end
                end
            end
        elseif event_ch == stream_event_ch then
            -- Here we save buffers that we've sent to players
            local stream_event = stream_event_ch:take()
            if stream_event.event == STREAM_EVENT_ACQUIRE_STARTED then
                track_begin = nil
                buffers = {}
                headers = {}
                track_end = nil
                bitrate = 0
                buffered_amount = 0
            elseif stream_event.event == STREAM_EVENT_TRACK_BEGIN then
                track_begin = {audio_type = stream_event.audio_type}
                buffers = {}
                headers = {}
                track_end = nil
                bitrate = 0
                buffered_amount = 0
                if stream_event.audio_type == 'o' then
                    save_header = true
                end
            elseif stream_event.event == STREAM_EVENT_BUFFER then
                if save_header and header_len(headers) < 8192 then
                    table.insert(headers,
                            {data = stream_event.data, size = stream_event.size})
                else
                    save_header = false
                    table.insert(buffers,
                            {data = stream_event.data, size = stream_event.size})
                    buffered_amount = buffered_amount + stream_event.size
                end
            elseif stream_event.event == STREAM_EVENT_FLUSH then
                buffers = {}
                buffered_amount = 0

            elseif stream_event.event == STREAM_EVENT_TRACK_END then
                track_end = true
            end
        elseif event_ch == rebuffer_request_ch then
            local players, new_cookie = unpack(rebuffer_request_ch:take())
            log:debug('Got rebuffer request')
            cookie_ch:put(new_cookie)
            do_rebuffer(shallowcopy(players),
                    new_cookie, track_begin, headers, buffers, track_end,
                    streaming_players_ch, player_states_ch)
            rebuffer_done_ch:put()
        end
    end

end)
end

function do_rebuffer(players, cookie, track_begin, headers, buffers, track_end,
        streaming_players_ch, player_states_ch)
    -- Always do this so we at least set the new cookie.
    log:debug('Rebuffer, stopping players')

    local failed_players = stop_players(players, cookie)
    for player_id, _ in pairs(failed_players) do
        players[player_id] = nil
    end
    if table_length(players) == 0 then
        return
    end

    log:debug('Rebuffer, waiting for cookie match')
    -- wait until cookie matches for all players
    local cookie_miss_count = 0
    while true do
        local cookie_matched_ids, non_matched_ids = unpack(player_states_ch:take())
        if is_subset_of(players, cookie_matched_ids) then
            break
        end
        for id, _ in pairs(players) do
            if not cookie_matched_ids[id] and not non_matched_ids[id] then
                -- we don't have the player anymore
                log:debug('do_rebuffer cookie_match dropping player: %s', id)
                players[id] = nil
                streaming_players_ch:put(players)
            end
        end
        if table_length(players) == 0 then
            return
        end

        cookie_miss_count = cookie_miss_count + 1
        if cookie_miss_count >= 10 then
            log:error('Rebuffer, cookie_miss_count too high, giving up.')
            for player_id, player in pairs(players) do
                if not cookie_matched_ids[player_id] then
                    log:info('Rebuffer, cookie_miss dropping player: %s',
                            player_id)
                    players[player_id] = nil
                end
            end
            break
        end
    end
    if table_length(players) == 0 then
        log:info('Rebuffer, out of players, returning')
        return
    end

    -- at this point we know our players matched the cookie, so we only need to
    -- check if they still match the cookie below

    if not track_begin then
        return
    end

    -- wait til we can track_begin all players
    log:debug('Rebuffer, waiting for track begin')
    local miss_count = 0
    while true do
        if all_vals(players, beep_player.player_can_track_begin) then
            break
        end
        local cookie_matched_ids = unpack(player_states_ch:take())
        for id, _ in pairs(players) do
            if not cookie_matched_ids[id] then
                log:debug('Rebuffer track_begin dropping player: %s', id)
                players[id] = nil
                streaming_players_ch:put(players)
            end
        end
        if table_length(players) == 0 then
            return
        end

        miss_count = miss_count + 1
        if miss_count >= 10 then
            log:error('Rebuffer, track_begin miss_count too high, giving up.')
            for player_id, player in pairs(players) do
                if not beep_player.player_can_track_begin(player) then
                    log:info('Rebuffer, track_begin miss dropping player: %s',
                            player_id)
                    players[player_id] = nil
                    player:_on_error(false, true)
                end
            end
            break
        end
    end
    if table_length(players) == 0 then
        return
    end

    log:debug('Rebuffer, calling track begin')
    -- track_begin all players
    for id, player in pairs(players) do
        if not player:track_begin(track_begin.audio_type) then
            players[id] = nil
        end
    end
    if table_length(players) == 0 then
        return
    end

    for _, buffer in ipairs(headers) do
        log:debug('Sending a header buffer')
        -- Just assume we can buffer at this point, it's a small amount of data
        for id, player in pairs(players) do
            if not player:buffer(buffer.data) then
                players[id] = nil
            end
        end
        if table_length(players) == 0 then
            return
        end
    end

    for _, buffer in ipairs(buffers) do
        local miss_count = 0
        while true do
            log:debug('Rebuffer, waiting for buffer')
            local all_can_buffer = all_vals(players,
                function(p)
                    return beep_player.player_can_buffer(p, buffer.size)
                end)
            if all_can_buffer then
                break
            end
            local cookie_matched_ids = unpack(player_states_ch:take())
            for id, _ in pairs(players) do
                if not cookie_matched_ids[id] then
                    log:debug('do_rebuffer waiting for buffer dropping player: %s', id)
                    players[id] = nil
                    streaming_players_ch:put(players)
                end
            end
            if table_length(players) == 0 then
                return
            end

            miss_count = miss_count + 1
            if miss_count >= 30 then
                for player_id, player in pairs(players) do
                    if not beep_player.player_can_buffer(player, buffer.size) then
                        log:info('Rebuffer, buffer miss dropping player: %s',
                                player_id)
                        players[player_id] = nil
                        player:_on_error(false, true)
                    end
                end
            end
            if table_length(players) == 0 then
                return
            end
        end
        log:debug('Rebuffer, calling buffer')

        for id, player in pairs(players) do
            if not player:buffer(buffer.data) then
                players[id] = nil
            end
        end
        if table_length(players) == 0 then
            return
        end
    end

    if not track_end then
        return
    end

    for id, player in pairs(players) do
        if not player:track_end() then
            players[id] = nil
        end
    end
end

function rebuffer_is_needed(players, new_players)
    log:debug('Checking if rebuffer needed: %s %s', keys(players), keys(new_players))
    local have_new_player = false
    for new_player_id, new_player in pairs(new_players) do
        if not players[new_player_id] or players[new_player_id] ~= new_player then
            have_new_player = true
            break
        end
    end
    if have_new_player then
        return true
    end
    return false
end


function state_thread(players_ch, player_states_ch,
        play_state_ch, play_event_ch, stream_event_ch,
        ubus_state_command_ch)
async.go(function()
    local players = {}
    local most_recent_stream_event = -1
    local last_reported_track_elapsed = 0
    local written_track_time_base = 0
    local track_elapsed_secs_base = 0

    while true do
        local event_ch = async.alts({
                players_ch, player_states_ch, play_state_ch, play_event_ch,
                stream_event_ch, ubus_state_command_ch})
        if event_ch == players_ch then
            local had_no_players = table_length(players) == 0
            players = players_ch:take()

            if had_no_players and table_length(players) > 0 then
                -- becoming master
                beepcloud.get_group('current_app', function(result)
                    if result then
                        set_app_ubus_obj(result)
                    end
                end)
            end

            -- TODO: we still manage volume directly via this global table, it's
            -- awkward to set it here.
            volume_players = players

            for id, player in pairs(players) do
                if not state.players[id] then
                    if global_volume then
                        update_player_state_volume(
                            id, player.name,
                            state.master_volume,
                            true)
                    else
                        update_player_state_volume(
                            id, player.name,
                            gain_to_volume(player.state.gain),
                            true)
                    end

                    trigger_update('device_added', {id = id})

                    if global_volume then
                        set_master_volume(state.master_volume)
                    end
                end
            end
            for id, player_state in pairs(state.players) do
                if not players[id] then
                    state.players[id] = nil
                    trigger_update('device_removed', {id = id})
                end
            end
            if table_length(players) == 0 then
                trigger_update('shutdown_source')
            end
        elseif event_ch == player_states_ch then
            local cookie_matched_ids = unpack(player_states_ch:take())
            local _, player1 = next(players)
            if is_subset_of(players, cookie_matched_ids) and player1 then
                local track_elapsed = math.floor(
                        player1.state.written_track_time / 1000)
                if most_recent_stream_event.event ~= STREAM_EVENT_ACQUIRE_STARTED
                        and track_elapsed ~= last_reported_track_elapsed then
                    last_track_advance_time = os.time()
                    last_reported_track_elapsed = track_elapsed
                    state.track_elapsed_secs = track_elapsed + track_elapsed_secs_base
                    state.streambuf_used =
                            STREAMBUF_SIZE - player1.state.streambuf_free_last_known
                    state.output_used = player1.state.output_used
                    state.written_track_time = player1.state.written_track_time
                        + written_track_time_base
                    trigger_update('progress', {
                        track_elapsed_secs=track_elapsed
                    })
                end
            end
        elseif event_ch == play_state_ch then
            log:info('GOT PLAY_STATE_CH event')
            local play_state = play_state_ch:take()
            if play_state == PLAY_STATE_PAUSED then
                state.audio_state = 'paused'
            elseif play_state == PLAY_STATE_AUTOSTART then
                state.audio_state = 'working'
            else  -- PLAY_STATE_PLAYING or PLAY_STATE_STARTING
                state.audio_state = 'playing'
            end
            trigger_update('audio_state_change')
        elseif event_ch == play_event_ch then
            log:info('GOT PLAY_EVENT_CH event')
            local play_event = play_event_ch:take()
            if play_event.event == PLAY_EVENT_DURATION_CHANGE then
                state.duration = play_event.duration
                trigger_update('duration_change')
            elseif play_event.event == PLAY_EVENT_TRACK_STARTED then
                -- This is private user info, don't log in production
                --log:info('TRACK STARTED: %s', play_event)
                state.play_state = 'playing'
                if play_event.track_info then
                    state.track_info = play_event.track_info
                    trigger_update('track_started')
                end
                -- if we don't have track_info we expect set_track_info will be
                -- called
            elseif play_event.event == PLAY_EVENT_TRACK_ENDED then
                log:info('TRACK ENDED')
                state.play_state = 'stopped'
                trigger_update('stopped')
                if app_ubus_obj then
                    ubus_call(conn, app_ubus_obj, 'audio_ended', nil, nil,
                            {method_not_found_ok=true})
                end
            end
        elseif event_ch == stream_event_ch then
            local stream_event = stream_event_ch:take()
            most_recent_stream_event = stream_event
            if stream_event.event == STREAM_EVENT_ACQUIRE_STARTED then
                last_reported_track_elapsed = 0

                written_track_time_base = 0
                track_elapsed_secs_base = 0

                state.written_track_time = 0
                state.streambuf_used = 0
                state.output_used = 0
                state.track_elapsed_secs = 0
                state.duration = 0

                if app_ubus_obj ~= stream_event.app_ubus_obj then
                    set_app_ubus_obj(stream_event.app_ubus_obj)
                    ubus_call(conn, 'beep.manager', 'notify_acquire',
                            {name=state.station.app})
                    state.track_info = nil
                    trigger_update('track_started')
                end

            elseif stream_event.event == STREAM_EVENT_REBUFFER then
                written_track_time_base = state.written_track_time
                track_elapsed_secs_base = state.track_elapsed_secs
            end
        elseif event_ch == ubus_state_command_ch then
            -- TODO: listen to stream_events_channel for token changed events
            -- use that to guard this functionality
            local state_command = ubus_state_command_ch:take()
            if state_command.method == 'set_station' then
                local msg = state_command.msg
                log:warn('GOT STATION: %s', msg)
                if msg.station_name ~= '' then
                    state.station.id = msg.station_id
                    state.station.name = msg.station_name
                    state.station.image_url = msg.station_image_url
                    -- if blank method passed then this cannot be saved as
                    -- a preset
                    if msg.play_station_method ~= '' then
                        state.station.method = msg.play_station_method
                        state.station.args = msg.play_station_args
                    end
                    history:add(state.station)
                end
                state.history = history:get_list()
                trigger_update('station_changed', state.station)
            elseif state_command.method == 'set_track_info' then
                -- This is private user info, don't log in production
                --log:debug('GOT SET_TI IN THREAD, %s', state_command)
                local ti = state_command.msg.track_info
                if not deep_equals(state.track_info, ti) then
                    -- This is private user info, don't log in production
                    --log:info('SETTING ti: %s', ti)
                    state.track_info = ti
                    trigger_update('track_started', state.track_info)
                end
            end
        end
    end
end)
end


-- Setup channels and threads
local ubus_acquire_ch = async.Channel(async.Buffer(), 'ubus_acquire')
local ubus_stream_command_ch = async.Channel(async.Buffer(), 'ubus_stream')
local ubus_stream_command_mult = async.Mult(ubus_stream_command_ch)
local ubus_players_command_ch = async.Channel(async.Buffer(), 'ubus_players')
local ubus_play_command_ch = async.Channel(async.Buffer(), 'ubus_play')
local ubus_state_command_ch = async.Channel(async.Buffer(), 'ubus_state')
local cookie_ch = async.Channel(async.SlidingBuffer(1))

local debug_ch = async.Channel(async.Buffer())
local debug_mult = async.Mult(debug_ch)

-- players channel is written to by players_thread and received by a handful of
-- other threads
local players_ch = async.Channel(async.SlidingBuffer(1))
local players_mult = async.Mult(players_ch)

local player_states_ch = async.Channel(async.SlidingBuffer(1))
local player_states_mult = async.Mult(player_states_ch)

local streaming_players_ch = async.Channel(async.Buffer())
local streaming_players_mult = async.Mult(streaming_players_ch)

local stream_event_ch = async.Channel(async.Buffer())
local stream_event_mult = async.Mult(stream_event_ch)

local play_state_ch = async.Channel(async.SlidingBuffer(1))
local play_state_mult = async.Mult(play_state_ch)

local play_event_ch = async.Channel(async.Buffer())

rebuffer_request_ch = async.Channel(async.SlidingBuffer(1))
rebuffer_done_ch = async.Channel(async.SlidingBuffer(1))

--async.go(function()
--    local ch = ubus_stream_command_mult:tap(async.Channel(async.Buffer()))
--    while true do
--        log:info('STREAM_COMMAND: %s', ch:take().method)
--    end
--end)

local players_thread = distributor_players.players_thread(
        ubus_players_command_ch, players_ch)
local player_states_thread = distributor_players.player_states_thread(
        players_mult:tap(async.Channel(async.SlidingBuffer(1))),
        cookie_ch, debug_mult:tap(), player_states_ch)
local stream_thread =  stream_thread(
        players_mult:tap(async.Channel(async.SlidingBuffer(1))),
        ubus_acquire_ch,
        ubus_stream_command_mult:tap(async.Channel(async.Buffer())),
        player_states_mult:tap(async.Channel(async.SlidingBuffer(1))),
        debug_mult:tap(),
        stream_event_ch,
        cookie_ch,
        streaming_players_ch)
local buffer_thread = buffer_thread(
        streaming_players_mult:tap(async.Channel(async.Buffer())),
        player_states_mult:tap(),
        stream_event_mult:tap(),
        rebuffer_request_ch,
        streaming_players_ch,
        cookie_ch,
        rebuffer_done_ch)
local play_thread = play_thread(
        streaming_players_mult:tap(async.Channel(async.Buffer())),
        stream_event_mult:tap(),
        player_states_mult:tap(async.Channel(async.SlidingBuffer(1))),
        ubus_play_command_ch,
        debug_mult:tap(),
        play_state_ch,
        play_event_ch)

local sync_thread = sync_thread(
        streaming_players_mult:tap(async.Channel(async.Buffer())),
        player_states_mult:tap(async.Channel(async.SlidingBuffer(1))),
        play_state_mult:tap(async.Channel(async.SlidingBuffer(1))))


local state_thread = state_thread(
    players_mult:tap(async.Channel(async.SlidingBuffer(1))),
    player_states_mult:tap(async.Channel(async.SlidingBuffer(1))),
    play_state_mult:tap(async.Channel(async.SlidingBuffer(1))),
    play_event_ch,
    stream_event_mult:tap(),
    ubus_state_command_ch)

async.set_gc_enabled(true)
async.start()


-- ubus object definition
local objects = {}
objects['beep.distributor'] = {
    -- required for all beep ubus objects
    get_state = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success(get_state()))
        end,
        {__unused = ubus.STRING}
    ),

    -- for use by manager
    add_player = beep_ubus_method(conn,
        function(req, msg)
            ubus_players_command_ch:put({
                method = 'add_player',
                req = req,
                msg = msg})
        end, {ip = ubus.STRING, id = ubus.STRING}  -- option: port = ubus.INT
    ),

    remove_device = beep_ubus_method(conn,
        function(req, msg)
            ubus_players_command_ch:put({
                method = 'remove_player',
                req = req,
                msg = msg})
        end, {id = ubus.STRING}
    ),

    remove_all_devices = beep_ubus_method(conn,
        function(req, msg)
            ubus_players_command_ch:put({
                method = 'remove_all_players',
                req = req,
                msg = msg})
        end, {__unused = ubus.STRING}
    ),

    -- for use by apps
    acquire = beep_ubus_method(conn,
        function(req, msg)
            ubus_acquire_ch:put({
                method = 'acquire',
                req = req,
                msg = msg})
        end, {app_ubus_obj = ubus.STRING}
    ),
    set_station = beep_ubus_method(conn,
        function(req, msg)
            ubus_state_command_ch:put({
                method = 'set_station',
                req = req,
                msg = msg})
            beep_reply2(req, beep_success())
        end,
        {token = ubus.INT32, station_id = ubus.STRING,
         station_name = ubus.STRING,
         play_station_method = ubus.STRING, play_station_args = ubus.TABLE}
    ),
    can_track_begin = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'can_track_begin',
                req = req,
                msg = msg})
        end,
        {token = ubus.INT32}
    ),
    track_begin = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'track_begin',
                req = req,
                msg = msg})
        end,
        {token = ubus.INT32, audio_type = ubus.STRING,
         content_length = ubus.INT32} -- optional: track_info = ubus.TABLE
    ),
    track_end = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'track_end',
                req = req,
                msg = msg})
        end,
        {token = ubus.INT32}
    ),
    set_track_info = beep_ubus_method(conn,
        function(req, msg)
            -- TODO: this needs token check
            log:debug('GOT SET_TI')
            ubus_state_command_ch:put({
                method = 'set_track_info',
                req = req,
                msg = msg})
            beep_reply(conn, req, beep_success())
        end,
        {token = ubus.INT32, track_info = ubus.TABLE}
    ),
    can_buffer = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'can_buffer',
                req = req,
                msg = msg})
        end,
        {token = ubus.INT32, data_len=ubus.INT32}
    ),
    buffer = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'buffer',
                req = req,
                msg = msg})
        end,
        {token = ubus.INT32, data=ubus.STRING}
    ),
    flush = beep_ubus_method(conn,
        function(req, msg)
            ubus_stream_command_ch:put({
                method = 'flush',
                req = req,
                msg = msg})
        end,
        {__unused = ubus.STRING}
    ),

    -- for use by controllers
    pause = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success())
            ubus_play_command_ch:put({
                method = 'app_pause',
                req = req,
                msg = msg})
        end,
        {__unused = ubus.STRING}
    ),

    do_pause = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success())
            ubus_play_command_ch:put({
                method = 'pause',
                req = req,
                msg = msg})
        end,
        {__unused = ubus.STRING}
    ),

    resume = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success())
            ubus_play_command_ch:put({
                method = 'resume',
                req = req,
                msg = msg})
        end,
        {__unused = ubus.STRING}
    ),

    smart_resume = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success())
            ubus_play_command_ch:put({
                method = 'app_resume',
                req = req,
                msg = msg})
        end,
        {__unused = ubus.STRING}
    ),

    skip = beep_ubus_method(conn,
        function(req, msg)
            if app_ubus_obj then
                ubus_call(conn, app_ubus_obj, 'skip', nil, nil,
                        {method_not_found_ok=true})
            end
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    prev = beep_ubus_method(conn,
        function(req, msg)
            if app_ubus_obj then
                ubus_call(conn, app_ubus_obj, 'prev', nil, nil,
                        {method_not_found_ok=true})
            end
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    set_volume = beep_ubus_method(conn,
        function(req, msg)
            -- check arguments first
            local volume_players = volume_available_players()
            for id, volume in pairs(msg.players) do
                if not volume_players[id] then
                    beep_reply(conn, req, beep_error(
                            'Invalid player key: ' .. id))
                    return
                end
                if volume < 0 or volume > 1000 then
                    beep_reply(conn, req, beep_error(
                            'Volume out of range [0, 1000] for key: ' .. id))
                    return
                end
            end

            if global_volume then
                for id, volume in pairs(msg.players) do
                    set_master_volume(volume)
                    break
                end
            else
                -- NOTE: A player could go away during these calls, depending
                -- on how removal is implemented.
                for id, volume in pairs(msg.players) do
                    set_player_volume(id, volume)
                end
            end

            beep_reply(conn, req, beep_success())
        end,
        {players = ubus.TABLE}
    ),

    adjust_volume = beep_ubus_method(conn,
        function(req, msg)
            -- check arguments first
            local volume_players = volume_available_players()
            for id, volume in pairs(msg.players) do
                if not volume_players[id] then
                    beep_reply(conn, req, beep_error(
                            'Invalid player key: ' .. id))
                    return
                end

                if volume < -1000 or volume > 1000 then
                    beep_reply(conn, req, beep_error(
                            'Volume out of range [0, 1000] for key: ' .. id))
                    return
                end
            end

            -- NOTE: A player could go away during these calls, depending
            -- on how removal is implemented.
            for id, volume in pairs(msg.players) do
                adjust_player_volume(id, volume)
            end

            beep_reply(conn, req, beep_success())
        end,
        {players = ubus.TABLE}
    ),

    adjust_local_volume = beep_ubus_method(conn,
        function(req, msg)
            local volume = msg.volume
            local volume_players = volume_available_players()
            if volume < -1000 or volume > 1000 then
                beep_reply(conn, req, beep_error(
                        'Volume out of range [0, 1000]'))
                return
            end

            if volume_players[local_device_id] then
                adjust_player_volume(local_device_id, volume)
            end

            beep_reply(conn, req, beep_success())
        end,
        {volume = ubus.INT32}
    ),

    set_master_volume = beep_ubus_method(conn,
        function(req, msg)
            local volume = msg.volume
            local client_id = msg.client_id or ''
            if volume < 0 or volume > 1000 then
                beep_reply(conn, req, beep_error(
                        'Volume out of range [0, 1000]'))
                return
            end

            set_master_volume(volume, client_id)

            beep_reply(conn, req, beep_success())
        end,
        {volume = ubus.INT32}  -- optional: client_id
    ),

    set_track_volume_scalar = beep_ubus_method(conn,
        function(req, msg)
            local volume = msg.volume
            local client_id = msg.client_id or ''
            if volume < 0 or volume > 1000 then
                beep_reply(conn, req, beep_error(
                        'Volume out of range [0, 1000]'))
                return
            end

            set_track_volume_scalar(volume)

            beep_reply(conn, req, beep_success())
        end,
        {volume = ubus.INT32}
    ),

    set_muted = beep_ubus_method(conn,
        function(req, msg)
            -- check arguments first
            local volume_players = volume_available_players()
            for id, is_muted in pairs(msg.players) do
                if not volume_players[id] then
                    beep_reply(conn, req, beep_error(
                            'Invalid player key: ' .. id))
                    return
                end
            end

            -- NOTE: A player could go away during these calls, depending
            -- on how removal is implemented.
            for id, is_muted in pairs(msg.players) do
                set_player_is_muted(id, is_muted)
            end

            beep_reply(conn, req, beep_success())
        end,
        {players = ubus.TABLE}
    ),

    play_history = beep_ubus_method(conn,
        function(req, msg)
            play_history(msg.index)
            beep_reply(conn, req, beep_success())
        end,
        {index = ubus.INT32, __unused = ubus.STRING}
    ),

    play_magic = beep_ubus_method(conn,
        function(req, msg)
            local index = 6
            local history_count = history:count()
            if history_count < index then
                index = history_count
            end
            play_history(index)
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    ----- debugging
    __dump = beep_ubus_method(conn,
        function(req, msg)
            log:info('Pending players:')
            for key, val in pairs(pending_players) do
                log:info(key)
            end
            log:info('Players:')
            for key, val in pairs(players) do
                log:info(key)
            end
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    __gc = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success({result = collectgarbage()}))
        end,
        {__unused = ubus.STRING}
    ),

    __gc_enable = beep_ubus_method(conn,
        function(req, msg)
            async.set_gc_enabled(true)
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    __gc_disable = beep_ubus_method(conn,
        function(req, msg)
            async.set_gc_enabled(false)
            beep_reply(conn, req, beep_success())
        end,
        {__unused = ubus.STRING}
    ),

    __gc_divisor = beep_ubus_method(conn,
       function(req, msg)
           local new_divisor = msg.divisor
           async.set_gc_divisor(new_divisor)
           beep_reply(conn, req, beep_success({divisor = new_divisor}))
       end,
       {divisor = ubus.INT32}
    ),

    __mem = beep_ubus_method(conn,
        function(req, msg)
            if msg.noforce then
                collectgarbage() -- Necessary for accurate measurement
            end
            local mem_count = collectgarbage('count')
            beep_reply(conn, req, beep_success({count = math.floor(mem_count * 1024)}))
        end,
        {__unused = ubus.STRING}
        -- OPTIONAL: noforce = ubus.BOOLEAN
    ),

    __profile = beep_ubus_method(conn,
        function(req, msg)
            require 'profiler'
            profiler.start('/tmp/distributor.luaprofile')
            log:debug('Starting profile')
            beep_reply(conn, req, beep_success())
            uloop.timer(function()
                log:debug('Stopping profile')
                profiler.stop()
            end, msg.duration_s * 1000)
        end,
        {duration_s = ubus.INT32}
    ),

    __thread_debug = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success())
            debug_ch:put(true)
        end,
        {__unused = ubus.STRING}
    )
}

---- on startup

conn:add(objects)

uloop.run()
