#!/usr/bin/env lua

-- Ref:
-- http://en.wikipedia.org/wiki/Chang_and_Roberts_algorithm

require 'util'

require 'ubus'
require 'beep_ubus'
require 'uloop'

local flags = require 'flags'
local inspect = require 'inspect'
local log = require 'log'

flags.add('uid', true, nil)

flags.init(arg)

if not flags.flags['uid'] then
    log:error('Must specify a uid!')
    os.exit(1)
end

local uid = tonumber(flags.flags['uid'])
local participant = false

uloop.init()

local conn = beep_ubus_connect('beepocrat')

local ubus_obj_path = 'beepocrat.' .. flags.flags['uid']
local objects = {}

local neighbor = uid
local neighbor_path = 'beepocrat.' .. neighbor

local send_cmd = function(cmd, n)
    ubus_call(conn, neighbor_path, cmd, {uid=n}, nil)
end

local send_election = function(n)
    participant = true
    send_cmd('election', n)
end

local send_elected = function(n)
    send_cmd('elected', n)
end

function check_slot(e_cur, e_next, e_new)
    if e_next > e_cur then
        return e_new > e_cur and e_new < e_next
    else
        return e_new > e_cur or e_new < e_next
    end
end

local id_new = uid
local id_cur
local id_next

function recursive_join(result, err)
    id_next = result['neighbor']
    if id_cur == id_new then
        print('-- I\'m already in this group, aborting --')
    elseif check_slot(id_cur, id_next, id_new) then
        print('-- Becoming neighbor to ' .. id_cur .. ', starting election --')
        ubus_call(conn,
                'beepocrat.' .. id_cur,
                'set_neighbor',
                {uid=id_new}, nil)
        neighbor = id_next
        neighbor_path = 'beepocrat.' .. neighbor
        send_election(uid)
    else
        print('-- Don\'t belong between ' .. id_cur .. ' and ' .. id_next .. ', moving on --')
        id_cur = id_next
        ubus_call(conn,
                'beepocrat.' .. id_next,
                'get_neighbor',
                nil,
                recursive_join)
    end
end

objects[ubus_obj_path] = {
    get_neighbor = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success({neighbor=neighbor}))
        end, {__unused = ubus.INT32}
    ),

    set_neighbor = beep_ubus_method(conn,
        function(req, msg)
            neighbor = msg['uid']
            neighbor_path = 'beepocrat.' .. neighbor
            beep_reply(conn, req, beep_success())
        end, {uid = ubus.INT32}
    ),

    join = beep_ubus_method(conn,
        function(req, msg)
            id_cur = msg['uid']
            ubus_call(conn,
                    'beepocrat.' .. id_cur,
                    'get_neighbor',
                    nil,
                    recursive_join)

            beep_reply(conn, req, beep_success())
        end, {uid = ubus.INT32}
    ),

    start = beep_ubus_method(conn,
        function(req, msg)
            send_election(uid)
            beep_reply(conn, req, beep_success())
        end, {__unused = ubus.INT32}
    ),

    election = beep_ubus_method(conn,
        function(req, msg)
            local other = msg['uid']
            if other > uid then
                print('Rec election, uid > mine, forwarding...')
                send_election(other)
            elseif other < uid then
                if not participant then
                    print(uid .. ' Rec election, uid < mine AND !participant, replace with mine...')
                    send_election(uid)
                else
                    print(uid .. ' Rec election, uid < mine AND participant, discarding...')
                end
            else
                print(uid .. ' **** I AM LEADER! ****')
                participant = false
                send_elected(uid)
            end
            beep_reply(conn, req, beep_success())
        end, {uid = ubus.INT32}
    ),

    elected = beep_ubus_method(conn,
        function(req, msg)
            local other = msg['uid']
            if other ~= uid then
                print(uid .. ' Rec elected, leader uid = ' .. tostring(other) .. ', forwarding...')
                send_elected(other)
            else
                print(uid .. ' Rec my own elected msg, finished.')
            end
            beep_reply(conn, req, beep_success())
        end, {uid = ubus.INT32}
    ) 
}

conn:add(objects)
uloop.run()

