module(..., package.seeall)

local inspect = require 'inspect'

require 'beep_ubus'
require 'util'

local async = require 'async'
local beep_player = require 'beep_player'
local log = require 'log'

local cookie

local function new_player(ip, port, id, name, playnet_path, done_ch, player_errors_ch)
    beep_player.new(ip, port, id, name, playnet_path,
        function(player)
            if player then
                done_ch:put(player)
            else
                done_ch:put(false)
                log:info('Could not connect player: %s', id)
            end
        end,
        function(player)  -- on error
            player_errors_ch:put(player)
        end)
    return done_ch
end

local function update_player_state(player, state_ch)
    player:update_state(
        function()  -- success
            player:log_state()
            state_ch:put(player.id)
        end,
        function()  -- error
            state_ch:put(false)
        end)
end

-- inputs: players_ch, cookie_ch
-- outputs: players_states_ch
-- TODO: invalid cookie count warning, either need to set player cookies to -1
--     when adding them, or only run this thread after initial acquire
-- TODO: the players that we get state for are not necessarily the players we're
-- streaming to, so the cookie check fails.
function player_states_thread(players_ch, cookie_ch, debug_ch, player_states_ch)
async.go(function()
    local players = {}
    local cookie = -1
    while true do
        local event_ch = async.alts(
                {players_ch, cookie_ch, debug_ch,
                async.timeout(100, 'player_state_timeout')})
        if event_ch == debug_ch then
            debug_ch:take()
            local our_locals = locals()
            local f = io.open('/tmp/distributor_player_states_thread.txt', 'w')
            f:write('PLAYER_STATES_THREAD LOCALS: ' .. inspect(our_locals) .. '\n')
            f:close()
        elseif event_ch == players_ch then
            players = players_ch:take()
        elseif event_ch == cookie_ch then
            cookie = cookie_ch:take()
            log:debug('GOT COOKIE: %s', cookie)
        else  -- timeout
            local state_ch = async.Channel(async.Buffer(), 'player_update_state')
            for id, player in pairs(players) do
                update_player_state(player, state_ch)
            end

            local cookie_matched_ids = {}
            local non_matched_ids = {}

            if next(players) ~= nil then
                local event_ch
                for i = 1, table_length(players) do
                    event_ch = async.alts({state_ch, players_ch})
                    if event_ch == players_ch then
                        -- abort the current update cycle
                        players = players_ch:take()
                        break
                    elseif event_ch == state_ch then
                        local player_id = state_ch:take()
                        local player = players[player_id]
                        if player then
                            if player.state.cookie == cookie then
                                cookie_matched_ids[player_id] = 1
                            else
                                non_matched_ids[player_id] = 1
                            end
                        end
                    end
                end

                -- only send the update if we didn't break out because we got
                -- a players_ch event.
                if event_ch ~= players_ch then
                    player_states_ch:put({cookie_matched_ids, non_matched_ids})
                end
            end
        end
    end
end)
end

-- inputs: ubus_players_command_ch
-- outputs: players_ch
function players_thread(ubus_players_command_ch, players_ch)
async.go(function()
    local players = {}
    local player_errors_ch = async.Channel(async.Buffer(), 'player_errors')
    local player_ready_ch = async.Channel(async.Buffer(), 'player_ready')
    local player_change_timeout_ch = nil

    while true do
        local channels = {player_errors_ch, player_ready_ch, ubus_players_command_ch}
        if player_change_timeout_ch then
            table.insert(channels, player_change_timeout_ch)
        end
        local event_ch = async.alts(channels)
        if event_ch == player_change_timeout_ch then
            player_change_timeout_ch:take()
            players_ch:put(shallowcopy(players))
            player_change_timeout_ch = nil
        elseif event_ch == ubus_players_command_ch then
            local player_command = ubus_players_command_ch:take()

            if player_command.method == 'add_player' then
                local player_id = player_command.msg.id
                local ip = player_command.msg.ip
                local port = player_command.msg.port
                local name = player_command.msg.name
                local playnet_path = player_command.msg.playnet_path
                if players[player_id] then
                    log:info('Already have player: %s:%s:%s', ip, port, player_id)
                    beep_reply2(player_command.req, beep_success())
                else
                    -- wait for player to connect
                    log:debug('WAITING FOR PLAYER TO CONNECT: %s', player_id)
                    local player = new_player(
                            ip, port, player_id, name, playnet_path,
                            player_ready_ch,
                            player_errors_ch)
                    beep_reply2(player_command.req, beep_success())
                end

            elseif player_command.method == 'remove_player' then
                local player_id = player_command.msg.id
                if not players[player_id] then
                    log:info('Device not found: %s', player_id)
                    beep_reply2(player_command.req, beep_success())
                else
                    players[player_id]:close()
                    players[player_id] = nil
                    log:info('Device removed: %s', player_id)
                    player_change_timeout_ch = nil
                    players_ch:put(shallowcopy(players))
                    beep_reply2(player_command.req, beep_success())
                end

            elseif player_command.method == 'remove_all_players' then
                log:info('Removing all players')
                for _, player in pairs(players) do
                    player:close()
                end
                players = {}
                player_change_timeout_ch = nil
                players_ch:put(shallowcopy(players))
                beep_reply2(player_command.req, beep_success())
            end
        elseif event_ch == player_ready_ch then
            local player = player_ready_ch:take()
            if player then
                log:debug('Player connected: %s', player.id)
                players[player.id] = player
                player_change_timeout_ch = async.timeout(
                        1000, 'player_change_timeout')
            end
        elseif event_ch == player_errors_ch then
            local error_player = player_errors_ch:take()
            local error_id = error_player.id
            if players[error_id] and players[error_id] == error_player then
                log:info('Removing player because of error: %s', error_id)
                players[error_id] = nil
                player_change_timeout_ch = nil
                players_ch:put(shallowcopy(players))
            elseif players[error_id] then
                log:debug('Got error for stale player: %s, ignoring', error_id)
            else
                log:debug('Did not have player to remove for error: %s', error_id)
            end
        end
    end
end)
end
