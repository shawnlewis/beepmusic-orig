#!/usr/bin/env lua

require "ubus"
require "uloop"

uloop.init()

local conn = ubus.connect()
if not conn then
    error("Failed to connect to ubus")
end

local objects = {}
objects['beep.app.test'] = {
    hello = {
        function(req, msg)
            conn:reply(req, {message="foo"});
            print("Call to function 'hello'")
            for k, v in pairs(msg) do
                print("key=" .. k .. " value=" .. tostring(v))
            end
        end, {id = ubus.INT32, msg = ubus.STRING }
    },
}

conn:add(objects)
uloop.run()
