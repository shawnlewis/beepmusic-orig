require 'uloop'
require 'util'

local inspect = require 'inspect'
local replay_log = require 'replay_log'
local umocks = require 'umocks'

local Player = {}
function Player:init(ip, port, id)
    self.id = id
end
function Player:close()
end
function Player:reset_sync_state()
end
function Player:call()
end
function Player:update_state()
end
function Player:track_begin()
end
function Player:track_end()
end
function Player:buffer()
end
function Player:flush()
end
function Player:start()
end
function Player:skip_ahead()
end
function Player:log_state()
end

local players = {}

function new_fake_player(...)
    local o = {}
    setmetatable(o, Player)
    Player.__index = Player
    o:init(...)
    players[o.id] = o
    return o
end

uloop = umocks.uloop()
ubus_conn = umocks.new_ubus_connection(
        uloop, 'beep.distributor')
ubus = umocks.ubus{connect_result=ubus_conn}


local beep_player = require 'beep_player'
beep_player.new = new_fake_player
package.loaded.beep_player = beep_player

local function run_line()
    local operation, id, val = replay_log.next_line()
    --print(operation, id)
    if not operation then
        return
    end
    if operation == 'channel_put' then
        local channel = replay_log.obj_reg[id]
        if not channel then
            print('REPLAY ERROR')
            os.exit(1)
        end
        if string_starts(id, 'player_ready') then
            val = players[val]
        elseif string_starts(id, 'ubus_stream') and val.method == 'buffer' then
            val.data = ''
            for i = 1, val.msg.data_len do
                val.data  = val.data .. 'a'
            end
        end
        --print(inspect(replay_log.obj_reg))
        channel:put(val)
    elseif operation == 'player_states' then
        for id, data in pairs(val) do
            local player = players[id]
            for k, v in pairs(data) do
                player[k] = v
            end
        end
    end
    uloop.timer(run_line, 1)
end

replay_log.run_replay('/tmp/distrib_replay.txt')

require 'distributor'

uloop.timer(run_line, 1)
uloop._timer_advance(10000000)
