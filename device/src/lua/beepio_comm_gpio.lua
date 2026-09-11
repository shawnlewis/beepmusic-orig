module(..., package.seeall)

local log = require 'log'

require 'gpio'

require 'beepio_bus'

local should_start = false

-- table of input callbacks, one for each gpio
local callbacks = {}

function input_callback(gpio, is_high)
    callbacks[gpio](is_high)
end

function init()
    gpio.init(input_callback, 1)
end

function enable_input(gpio_num, active_low, change_callback)
    should_start = true
    callbacks[gpio_num] = function(is_high)
        if active_low then
            change_callback(not is_high)
        else
            change_callback(is_high)
        end
    end
    gpio.enable_input(gpio_num)
end

function enable_output(gpio_num)
    should_start = true
    gpio.enable_output(gpio_num)
end

function start()
    if should_start then
        gpio.start()
    end
end

function set_led(gpio_num, val)
    gpio.set_led(gpio_num, val)
end

function commit()
    gpio.commit()
end
