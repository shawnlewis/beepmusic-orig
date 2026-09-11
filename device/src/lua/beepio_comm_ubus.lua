module(..., package.seeall)

local log = require 'log'

require 'beep_ubus'

require 'beepio_bus'

local ubus_conn

function set_ubus_conn(conn)
    ubus_conn = conn
end

local inited = false
function init()
    if not inited then
        local ubus_objects = {}
        ubus_objects['beep.io.sequence'] = {
            send = beep_ubus_method(ubus_conn,
                function(req, msg)
                    for i = 1, #msg.seq do
                        beepio_bus.send_event('ubus.key', msg.seq:sub(i, i))
                    end
                    beep_reply(ubus_conn, req, beep_success())
                end,  {seq = ubus.STRING}
            )
        }
        ubus_conn:add(ubus_objects)
    end
    inited = true
end
