#!/usr/bin/env lua

require 'ubus'
require 'uloop'

require 'beep_ubus'
require 'util'

local flags = require 'flags'
local log = require 'log'

flags.init(arg)
log:init('app_testtarget')
uloop.init()

local conn = beep_ubus_connect('app_testtarget')
local senders = {}

local function send_message(sender_id, message, namespace, result_cb)
    ubus_call(conn, 'beep.comm', 'msg_socket_send_message',
            {app_id = 'testtarget', sender_id = sender_id,
            namespace = namespace, message = message}, function(result)
                if result_cb ~= nil then
                    result_cb(result)
                end
    end)
end

local objects = {}
objects['beep.app.testtarget'] = {
    get_state = beep_ubus_method(conn,
        function(req, msg)
            beep_reply(conn, req, beep_success({unused = true}))
        end, {__unused = ubus.STRING}
    ),

    msg_socket_sender_connected = beep_ubus_method(conn,
        function(req, msg)
            local sender_id = msg['sender_id']
            local user_agent = msg['user_agent']

            if senders[sender_id] then
                log:warn('Sender %s was already connected! Updating record...',
                        sender_id)
            end

            senders[sender_id] = user_agent

            log:info('Sender connected: %s (%s)', sender_id, user_agent)

            beep_reply(conn, req, beep_success())
        end, {sender_id = ubus.STRING, user_agent = ubus.STRING}
    ),

    msg_socket_sender_disconnected = beep_ubus_method(conn,
        function(req, msg)
            local sender_id = msg['sender_id']

            if not senders[sender_id] then
                log:warn('Sender %s not found.', sender_id)
            else
                log:info('Sender disconnected: %s', sender_id)
                senders[sender_id] = nil
            end

            beep_reply(conn, req, beep_success())
        end, {sender_id = ubus.STRING}
    ),

    msg_socket_message_received = beep_ubus_method(conn,
        function(req, msg)
            local sender_id = msg['sender_id']
            local namespace = msg['namespace']
            local message = msg['message']

            log:info('Message received: %s says %s (%s)',
                    sender_id, message, namespace)

            if not senders[sender_id] then
                log:warn('Sender %s not registered', sender_id)
            end

            if string.sub(message,1,4) == 'ECHO' then
                send_message(sender_id, message, namespace, function(result)
                    if not result then
                        log:error('Failed to send message')
                        beep_reply(conn, req, beep_error('send message failed'))
                    else
                        log:info('Echo message to %s: %s (%s)',
                            sender_id, message, namespace)
                        beep_reply(conn, req, beep_success())
                    end
                end)
            else
                beep_reply(conn, req, beep_success())
            end
        end, {sender_id = ubus.STRING, namespace = ubus.STRING,
                message = ubus.STRING}
    ),

    _send_message = beep_ubus_method(conn,
        function(req, msg)
            local message = msg['message'] or 'Hello, world!'
            local namespace = msg['namespace'] or 'default_namespace'
            local sender_id = msg['sender_id']

            -- Manually check types for optional arguments
            if type(message) ~= 'string' then
                beep_reply(conn, req, beep_error('message must be a string'))
                return
            end

            if type(message) ~= 'string' then
                beep_reply(conn, req, beep_error('sender_id must be a string'))
                return
            end

            if sender_id then
                if type(sender_id) ~= 'string' then
                    beep_reply(conn, req,
                            beep_error('sender_id must be a string'))
                    return
                end
            else
                -- Autoselect first sender
                for k,_ in pairs(senders) do
                    sender_id = k
                    break
                end
                if not sender_id then
                    log:error('No sender_id available')
                    beep_reply(conn, req, beep_error('no senders available'))
                    return
                end
            end

            send_message(sender_id, namespace, message, function(result)
                if not result then
                    log:error('Failed to send message')
                    beep_reply(conn, req, beep_error('send message failed'))
                else
                    log:info('Sent message to %s: %s (%s)',
                        sender_id, message, namespace)
                    beep_reply(conn, req, beep_success())
                end
            end)
        end, {__unused = ubus.STRING}
            -- Optional arg: 'message' = ubus.STRING (default 'Hello, world!'),
            -- Optional arg: 'sender_id' = ubus.STRING (default autoselect)
            -- Optional arg: 'namespace' = ubus.String (default 'default_namespace')
    ),
}

conn:add(objects)
uloop.run()
