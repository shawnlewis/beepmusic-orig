module(..., package.seeall)

require 'util'

local log = require('log')

require('beepio_registry')
require('beepio_comm_beepdial')
require('beepio_bus')

-----
-- Base Input
-----

class('Input')
function Input:_init(name, args)
    self.NAME = name
    self.args = args
end


-----
-- Base input types
-----

class('Button', Input)
Button.TYPE = 'button'
function Button:_init(...)
    Input._init(self, ...)
    self.down = false
end
function Button:send_button_events(down_count, up_count)
    while up_count ~= 0 or down_count ~= 0 do
        if self.down then
            if up_count ~= 0 then
                up_count = up_count - 1
                self.down = false
                beepio_bus.send_event('button.up', self)
            else
                log:error('Got invalid button counts with is_down (%s): %s %s',
                        tonumber(self.down), down_count, up_count)
                return
            end
        else
            if down_count ~= 0 then
                down_count = down_count - 1
                self.down = true
                beepio_bus.send_event('button.down', self)
            else
                log:error('Got invalid button counts with is_down (%s): %s %s',
                        tonumber(self.down), down_count, up_count)
                return
            end
        end
    end
end

class('RotaryEncoder', Input)
RotaryEncoder.TYPE = 'rotary'
function RotaryEncoder:send_knob_event(delta)
    beepio_bus.send_event('rotary.turn', self, delta)
end


class('PowerSourceState', Input)
PowerSourceState.TYPE = 'power_source_state'
PowerSourceState.STATE_AC = 0
PowerSourceState.STATE_BATTERY = 1
function PowerSourceState._init(self, ...)
    Input._init(self, ...)
    self._state = -1
end

function PowerSourceState:send_state(state)
    if self._state == state then
        return
    end
    self._state = state
    if state == PowerSourceState.STATE_AC then
        beepio_bus.send_event('power_source_state.ac')
    elseif state == PowerSourceState.STATE_BATTERY then
        beepio_bus.send_event('power_source_state.battery')
    else
        log:error('PowerSourceState got invalid state: %s', state)
    end
end


-----
-- Beep dial inputs
-----

class('beepdial_knob_button', Button)
beepio_registry.register_input(beepdial_knob_button)

function beepdial_knob_button:_init(...)
    Input._init(self, ...)
    beepio_comm_beepdial.init()
    beepio_comm_beepdial.set_button_callback(function (down_count, up_count)
        self:send_button_events(down_count, up_count)
    end)
end


class('beepdial_knob', RotaryEncoder)
beepio_registry.register_input(beepdial_knob)

function beepdial_knob:_init(...)
    self.__base:_init(...)
    beepio_comm_beepdial.init()
    beepio_comm_beepdial.set_knob_callback(function (delta)
        self:send_knob_event(delta)
    end)
end


-----
-- Generic gpio inputs
-----

class('gpio_button', Button)
beepio_registry.register_input(gpio_button)

function gpio_button:_init(...)
    Input._init(self, ...)
    beepio_comm_gpio.enable_input(
        tonumber(self.args.gpio), tonumber(self.args.active_low),
        function(is_pressed)
            if is_pressed then
                self:send_button_events(1, 0)
            else
                self:send_button_events(0, 1)
            end
        end)
end


-----
-- Generic i2c inputs
-----

class('i2c_button', Button)
beepio_registry.register_input(i2c_button)

function i2c_button:_init(...)
    Input._init(self, ...)
    beepio_comm_i2c.enable_input(
        tonumber(self.args.id),
        'button',
        function(down_count, up_count)
            self:send_button_events(down_count, up_count)
        end)
end


class('i2c_power_source_state', PowerSourceState)
beepio_registry.register_input(i2c_power_source_state)

function i2c_power_source_state:_init(...)
    PowerSourceState._init(self, ...)
    beepio_comm_i2c.enable_input(
        tonumber(self.args.id),
        'power_state',
        function(state)
            log:info('i2c_power_source_state callback: %s', state)
            if state == 0 then
                self:send_state(PowerSourceState.STATE_AC)
            elseif state == 1 then
                self:send_state(PowerSourceState.STATE_BATTERY)
            else
                log:error('Got unexpected i2c power state')
            end
        end)
end


-----
-- Ubus inputs (for testing)
-----

class('UbusButton', Button)
function UbusButton:_init(...)
    Input._init(self, ...)
    beepio_bus.add_handler('ubus.key', 1, bind(self, 'on_ubus_key'))
end

function UbusButton:on_ubus_key(time, key)
    if key == self.DOWN_KEY then
        self:send_button_events(1, 0)
    elseif key == self.UP_KEY then
        self:send_button_events(0, 1)
    end
end

class('ubus_button1', UbusButton)
beepio_registry.register_input(ubus_button1)
ubus_button1.DOWN_KEY = 'a'
ubus_button1.UP_KEY = 'b'

class('ubus_button2', UbusButton)
beepio_registry.register_input(ubus_button2)
ubus_button2.DOWN_KEY = 'c'
ubus_button2.UP_KEY = 'd'

class('ubus_button3', UbusButton)
beepio_registry.register_input(ubus_button3)
ubus_button3.DOWN_KEY = 'e'
ubus_button3.UP_KEY = 'f'

class('ubus_rotary', RotaryEncoder)
beepio_registry.register_input(ubus_rotary)
function ubus_rotary:_init(...)
    Input._init(self, ...)
    beepio_bus.add_handler('ubus.key', 1, bind(self, 'on_ubus_key'))
end

function ubus_rotary:on_ubus_key(time, key)
    if key == 'g' then
        self:send_knob_delta(-1)
    elseif key == 'h' then
        self:send_knob_delta(1)
    end
end
