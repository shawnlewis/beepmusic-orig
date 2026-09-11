module(..., package.seeall)

require 'util'

local log = require('log')

require('beepio_registry')
require('beepio_comm_beepdial')
require('beepio_comm_i2c')
require('beepio_bus')

-----
-- Base Output
-----

class('Output')
function Output:_init(name, args)
    self.NAME = name
    self.args = args
end


-----
-- Base output types
-----


class('LedRing24', Output)
LedRing24.TYPE = 'led_ring24'

class('Led', Output)
Led.TYPE = 'led'

class('RGBLed', Output)
RGBLed.TYPE = 'rgb_led'

-----
-- Beep dial output
-----

class('beepdial_led_ring24', LedRing24)
beepio_registry.register_output(beepdial_led_ring24)

function beepdial_led_ring24:_init(...)
    Output._init(self, ...)
    beepio_comm_beepdial.init()
end

function beepdial_led_ring24:set_all_leds(val)
    beepio_comm_beepdial.set_all_leds(val)
end

function beepdial_led_ring24:set_led(n, val)
    beepio_comm_beepdial.set_led(n, val)
end


-----
-- Generic gpio outputs
-----

class('gpio_led', Led)
beepio_registry.register_output(gpio_led)

local needs_commit = false
local tick_callback_is_set = false

function gpio_led:_init(...)
    Output._init(self, ...)
    self.gpio = tonumber(self.args.gpio)
    beepio_comm_gpio.enable_output(self.gpio)

    if not tick_callback_is_set then
        beepio_clock.add_tick_callback(function()
            if needs_commit then
                beepio_comm_gpio.commit()
            end
            needs_commit = false
        end)
        tick_callback_is_set = true
    end
end

-- number between 0 and 1000
function gpio_led:set(val)
    beepio_comm_gpio.set_led(self.gpio, val)
    needs_commit = true
end


-----
-- Generic i2c outputs
-----

class('i2c_led', Led)
beepio_registry.register_output(i2c_led)

function i2c_led:_init(...)
    Output._init(self, ...)
    self.i2c_id = tonumber(self.args.id)
    beepio_comm_i2c.enable_output(self.i2c_id, 'led')
end

function i2c_led:set(val)
    beepio_comm_i2c.set_led(self.i2c_id, val)
end


class('i2c_rgb_led', RGBLed)
beepio_registry.register_output(i2c_rgb_led)

function i2c_rgb_led:_init(...)
    Output._init(self, ...)
    self.i2c_id = tonumber(self.args.id)
    beepio_comm_i2c.enable_output(self.i2c_id, 'rgb_led')
end

function i2c_rgb_led:set(r, g, b)
    beepio_comm_i2c.set_rgb_led(self.i2c_id, r, g, b)
end


-----
-- Test outputs
-----

class('log_led_ring24', LedRing24)
beepio_registry.register_output(log_led_ring24)

function log_led_ring24:set_all_leds(val)
    log:debug('log_led_ring24: Setting all leds to %s', val)
end

function log_led_ring24:set_led(n, val)
    log:debug('log_led_ring24: Setting led %s to %s', n, val)
end
