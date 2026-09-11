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

describe('Distributor', function()

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
describe('health', function()
    it('works', function()
        -- This will handle any calls that distributor makes to
        -- beep.health:pong. It will delay for 10 time units before running.
        -- We can check (below) that it was called.
        local health_pong = ubus_conn:_set_call_handler(
            'beep.health', 'pong', 10, beep_success())

        -- distributor is listening for beep.ping, trigger it by calling
        -- the listener directly.
        ubus_conn._listeners['beep.ping']()

        -- the ping method waits between 0-200ms before responding.
        uloop._timer_advance(300)

        -- make sure 'beep.health' 'pong' was called
        assert.spy(health_pong).called(1)
    end)
end)

describe('volume', function()
    it('volume to gain conversion works', function()
        -- we don't test values below 100 for now, because multiple volumes
        -- map to a single gain below for volume values below 100.
        for vol = 100, 1000 do
            assert.equal(vol, gain_to_volume(volume_to_gain(vol)))
        end
    end)

    it('works', function()
        ---- TODO: distributor should be refactored so we can setup players in
        ----     different states.
        --local playnet_hello = ubus_conn:_set_call_handler(
        --    'beep.playnet', 'hello', 10,
        --    beep_success({stream_port=19999, status={
        --        gain=88, streambuf_free=1024 * 1024}}))
        --local playnet_stop = ubus_conn:_set_call_handler(
        --    'beep.playnet', 'stop', 10,
        --    beep_success())

        --local manager_notify_acquire = ubus_conn:_set_call_handler(
        --    'beep.manager', 'notify_acquire', 10,
        --    beep_success())

        --local called_with_gain
        --local playnet_set_volume = ubus_conn:_set_call_handler(
        --        'beep.playnet', 'set_volume', 10,
        --        function(msg, done_cb)
        --            called_with_gain = msg.gain
        --            done_cb(beep_success())
        --        end)

        ---- This calls one of the methods provided by distributor. We can
        ---- check the reply from the method later.
        ---- using ip='local', avoids urelay
        --local add_player_call = ubus_conn:_call(
        --    'add_player', {id='p1', ip='local', port=4333})
        ----assert.is_truthy(connecting_players['p1'])

        --uloop._timer_advance(10)
        --assert.spy(playnet_hello).called(1)

        --uloop._timer_advance(10)
        --assert.spy(playnet_stop).called(1)

        ---- Check the add_player reply.
        --assert.is_true(ubus_conn:_get_reply(add_player_call).success)
        ----assert.is_falsy(connecting_players['p1'])
        --assert.is_truthy(pending_players['p1'])

        ---- Should be able to set volume and receive an update when player
        ---- is pending
        --ubus_conn:_clear_events()
        --playnet_set_volume.calls = {}
        --local set_volume_call = ubus_conn:_call(
        --        'set_volume', {players={p1=15}})
        --uloop._timer_advance(10)
        --assert.spy(playnet_set_volume).called(1)
        --assert.is_true(ubus_conn:_get_reply(set_volume_call).success)

        ---- TODO: we used to wait for playnet gain changes before sending volume
        ---- updates. Leaving this here because it might be a good example for
        ---- something else.
        ----ubus_conn:_set_call_handler('beep.playnet', 'get_state', 5, {
        ----        streambuf_free = 1024 * 1024,
        ----        num_tracks_started = 0,
        ----        gain = called_with_gain,
        ----        sync_played_time = 0,
        ----        written_track_time = 0
        ----    })
        ----uloop._timer_advance(110)

        --local volume_event = ubus_conn._events[1]
        --assert.equal('volume_changed', volume_event.msg.event_type)
        --assert.equal(15, volume_event.msg.state.players.p1.volume)

        ---- acquire to move the player to players
        --local acquire_call = ubus_conn:_call(
        --        'acquire', {app_ubus_obj='test'})
        ---- TODO: player actually is moved to pending immediately even
        ----     though this call takes 10 to complete due to it's call to
        ----     beep.playnet:stop on the players. Maybe this is a bug?
        --uloop._timer_advance(20)
        --assert.is_true(ubus_conn:_get_reply(acquire_call).success)
        --assert.is_falsy(pending_players['p1'])
        --assert.is_truthy(players['p1'])

        --ubus_conn:_clear_events()
        --playnet_set_volume.calls = {}
        --local set_volume_call = ubus_conn:_call(
        --        'set_volume', {players={p1=17}})
        --uloop._timer_advance(10)
        --assert.spy(playnet_set_volume).called(1)
        --assert.is_true(ubus_conn:_get_reply(set_volume_call).success)

        ----ubus_conn._listeners['beep.state.playnet._local_'](
        ----    'beep.state.playnet._local_', {
        ----        streambuf_free = 1024 * 1024,
        ----        num_tracks_started = 0,
        ----        gain = called_with_gain,
        ----        sync_played_time = 0,
        ----        written_track_time = 1000
        ----    })
        ----
        --local volume_event = ubus_conn._events[1]
        --assert.equal('volume_changed', volume_event.msg.event_type)
        --assert.equal(17, volume_event.msg.state.players.p1.volume)
    end)
end)

describe('audio', function()
    it('works', function()
        local playnet_hello = ubus_conn:_set_call_handler(
            'beep.playnet', 'hello', 10,
            beep_success({stream_port=19999, status={
                gain=88,
                streambuf_free = 1024 * 1024,
                can_st_begin = true
            }}))
        local playnet_stop = ubus_conn:_set_call_handler(
            'beep.playnet', 'stop', 10,
            beep_success())
        local playnet_set_volume = ubus_conn:_set_call_handler(
                'beep.playnet', 'set_volume', 10, beep_success())
        local manager_notify_acquire = ubus_conn:_set_call_handler(
            'beep.manager', 'notify_acquire', 10,
            beep_success())

        local add_player_call = ubus_conn:_call(
            'add_player', {id='p1', ip='local', port=4333})
        --assert.is_truthy(connecting_players['p1'])

        uloop._timer_advance(20)
        assert.is_true(ubus_conn:_get_reply(add_player_call).success)

        local acquire_call = ubus_conn:_call(
                'acquire', {app_ubus_obj='test'})
        uloop._timer_advance(20)
        local acquire_reply = ubus_conn:_get_reply(acquire_call)
        assert.is_true(acquire_reply.success)
        local token = acquire_reply.result.token

        local track_begin_call = ubus_conn:_call(
                'track_begin', {token = token, audio_type = 'm', content_length=0})
        uloop._timer_advance(1)
        -- should return immediately
        assert.is_true(ubus_conn:_get_reply(track_begin_call).success)
    end)
end)

end)  -- describe 'distributor'

