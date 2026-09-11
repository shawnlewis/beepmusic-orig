require('util')

local inspect = require 'inspect'
local umocks = require 'umocks'

local log = require 'log'

-- busted defines a function called async, get rid of since we want to import
-- our library called async, which will fail otherwise.
busted_async = async
async = nil


-- mocks
uloop = nil
local ubus_conn = nil
ubus = nil
stream = nil

local fail_on_log_error
local log_lines = {}
local old_log_write = log.log_write
log.log_write = function(color, level, line)
    table.insert(log_lines, {level=level, line=line})
    old_log_write(color, level, line)
end

function test_log_file()
    local test_name = test_name()
    test_name = string.gsub(test_name, ' ', '')
    test_name = string.gsub(test_name, '/', '-')
    return '/tmp/lua_test_' .. test_name .. '.log'
end

PlayerStub = make_class()
function PlayerStub:_init()
    self.prev_state = {}
    self.state = {
        content_length = 0, num_tracks_started = 0, written_track_time = 0,
        output_used = 0, bitrate = 0}
end
function PlayerStub:call()
end
function PlayerStub:_set_state(state)
    self.prev_state = deepcopy(self.state)

    for k, v in pairs(state) do
        self.state[k] = v
    end
end

describe('Distributor async', function()

before_each(function()
    log:set_output(io.open(test_log_file(), 'w'))

    -- child tests can override this
    fail_on_log_error = true
    log_lines = {}

    -- Create fresh mocks.
    uloop = umocks.uloop()
    ubus_conn = umocks.new_ubus_connection(
            uloop, 'beep.distributor')
    ubus = umocks.ubus{connect_result=ubus_conn}
    stream = umocks.stream()

    -- Reload the modules under test to ensure fresh state.
    package.loaded.beep_ubus = nil
    require 'beep_ubus'
    package.loaded.distributor = nil
    require 'distributor'
    package.loaded.async = nil
    async = require 'async'
end)

-- busted doesn't properly handle assertions and errors in after_each
-- clauses (it just hangs), so we modify the it function instead.
local old_it = it
it = function(test_name, test_func)
    old_it(test_name, function()
        test_func()
        if fail_on_log_error then
            for i, log_line in ipairs(log_lines) do
                if log_line.level == 'error' then
                    error('Test logged an error. See ' .. test_log_file())
                end
            end
        end
    end)
end


-- These are test groups using the 'busted' unittest framework.
-- it() defines a test.
describe('player_thread', function()
    local players_ch
    local player_states_ch
    local ubus_play_command_ch
    local stream_event_ch
    local play_state_ch
    local play_event_ch

    before_each(function()
        players_ch = async.Channel(async.Buffer())
        player_states_ch = async.Channel(async.Buffer())
        ubus_play_command_ch = async.Channel(async.Buffer())
        stream_event_ch = async.Channel(async.Buffer())
        play_state_ch = async.Channel(async.Buffer())
        play_event_ch = async.Channel(async.Buffer())

        local play_thread = play_thread(
            players_ch, player_states_ch, ubus_play_command_ch, stream_event_ch,
            play_state_ch, play_event_ch)
        async.start()
    end)

    describe('duration', function()
        it('change_on_bitrate', function()
            local player = PlayerStub()
            players_ch:put({player})

            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN, content_length=1024*1024})
            uloop._timer_advance(1)

            player:_set_state({output_used = 0, written_track_time=0, bitrate=128})
            player_states_ch:put(true)

            uloop._timer_advance(1)

            -- expect duration change
            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_DURATION_CHANGE, play_event.event)
            assert.equal(64, play_event.duration)

            ubus_conn:_clear_events()

            -- no more duration change events
            player_states_ch:put(true)
            uloop._timer_advance(1)
            assert.equal(nil, play_event_ch:take_async())

            -- until the next track begin
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN, content_length=2048*1024})
            uloop._timer_advance(1)
            assert.equal(nil, play_event_ch:take_async())

            -- which must be followed by player_state_ch update
            player_states_ch:put(true)
            uloop._timer_advance(1)
            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_DURATION_CHANGE, play_event.event)
            assert.equal(128, play_event.duration)
        end)

        it('no_content_length_means_zero_duration', function()
            local player = PlayerStub()
            players_ch:put({player})

            player:_set_state({output_used = 0, written_track_time=0, bitrate=128})
            player_states_ch:put(true)

            uloop._timer_advance(1)

            -- expect duration change
            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_DURATION_CHANGE, play_event.event)
            assert.equal(0, play_event.duration)
        end)

        it('change_on_written_track_time_advance', function()
            local player = PlayerStub()
            players_ch:put({player})

            -- must move to PLAY_STATE_AUTOSTART
            ubus_play_command_ch:put({method = 'resume'})
            uloop._timer_advance(1)
            assert.equal(PLAY_STATE_AUTOSTART, play_state_ch:take_async())

            -- Then move to PLAY_STATE_STARTING
            player:_set_state({output_used = 1000000, written_track_time=0, bitrate=0})
            player_states_ch:put(true)
            uloop._timer_advance(1)
            assert.equal(PLAY_STATE_STARTING, play_state_ch:take_async())

            -- No duration change yet
            assert.equal(nil, play_event_ch:take_async())

            -- Then advance written_track_time also moves to PLAY_STATE_PLAYING
            player:_set_state({output_used = 0, written_track_time=1000, bitrate=0})
            player_states_ch:put(true)
            uloop._timer_advance(1)
            assert.equal(PLAY_STATE_PLAYING, play_state_ch:take_async())

            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_DURATION_CHANGE, play_event.event)
            assert.equal(0, play_event.duration)
        end)
    end)

    describe('track_started', function()
        it('works', function()
            local player = PlayerStub()
            players_ch:put({player})

            -- an acquire
            stream_event_ch:put({event = STREAM_EVENT_ACQUIRE_DONE})
            uloop._timer_advance(1)

            -- then a track_begin
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN,
                 track_info = 'track_info_1'})
            uloop._timer_advance(1)

            -- then a track started increment
            player:_set_state({num_tracks_started=1})
            player_states_ch:put(true)
            uloop._timer_advance(1)

            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_TRACK_STARTED, play_event.event)
            assert.equal('track_info_1', play_event.track_info)

            -- another track_begin
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN,
                 track_info = 'track_info_2'})
            uloop._timer_advance(1)

            -- another track started increment
            player:_set_state({num_tracks_started=2})
            player_states_ch:put(true)
            uloop._timer_advance(1)

            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_TRACK_STARTED, play_event.event)
            assert.equal('track_info_2', play_event.track_info)
        end)

        it('acquire flushes', function()
            -- more than one TRACK_BEGIN should cause tracks to be buffered up,
            -- we send them as they start playing.
            local player = PlayerStub()
            players_ch:put({player})

            -- an acquire
            stream_event_ch:put({event = STREAM_EVENT_ACQUIRE_DONE})
            uloop._timer_advance(1)

            -- then a track_begin
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN, track_info = 'track_info_1'})
            uloop._timer_advance(1)

            -- then a track started increment
            player:_set_state({num_tracks_started=1})
            player_states_ch:put(true)
            uloop._timer_advance(1)
            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_TRACK_STARTED, play_event.event)
            assert.equal('track_info_1', play_event.track_info)

            -- one more TRACK_BEGIN and then an acquire
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN, track_info = 'track_info_2'})
            uloop._timer_advance(1)

            stream_event_ch:put({event = STREAM_EVENT_ACQUIRE_DONE})
            uloop._timer_advance(1)

            -- should be flushed now, do another TRACK_BEGIN and then increment
            -- num_tracks_started
            stream_event_ch:put(
                {event = STREAM_EVENT_TRACK_BEGIN, track_info = 'track_info_3'})
            uloop._timer_advance(1)

            player:_set_state(
                {output_used = 0, written_track_time=0, num_tracks_started=1})
            player_states_ch:put(true)
            uloop._timer_advance(1)
            local play_event = play_event_ch:take_async()
            assert.equal(PLAY_EVENT_TRACK_STARTED, play_event.event)
            assert.equal('track_info_3', play_event.track_info)
        end)
    end)
end)

end)  -- describe 'Distributor async'
