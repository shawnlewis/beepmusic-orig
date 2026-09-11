module(..., package.seeall)

require 'util'

local log = require 'log'

local registry = require('beepio_registry')
require('beepio_bus')
require('beepio_model') -- TODO not required once volume is fixed.

class('InputTrigger')
function InputTrigger:_init(inputs, args)
    if #inputs == 1 then
        self.input = inputs[1]
    else
        self.inputs = inputs
    end
end

function InputTrigger:check_inputs()
    local inputs, input_types
    if self.input then
        inputs = {self.input}
        input_types = {self.INPUT_TYPE}
    else
        inputs = self.inputs
        input_types = self.INPUT_TYPES
    end
    local result = self:_check_inputs(inputs, input_types)
    if not result then
        log:error('InputTrigger %s expects input types %s but got inputs %s',
                self.__name, input_types, inputs)
    end
    return result
end

function InputTrigger:_check_inputs(inputs, input_types)
    if #inputs ~= #input_types then
        return false
    end
    for i = 1, #inputs do
        if inputs[i].TYPE ~= input_types[i] then
            return false
        end
    end
    return true
end

class('ButtonInputTrigger', InputTrigger)
ButtonInputTrigger.INPUT_TYPE = 'button'

class('RotaryInputTrigger', InputTrigger)
RotaryInputTrigger.INPUT_TYPE = 'rotary'


-- TODO: store button this trigger is attached to, and check it.
class('tap', ButtonInputTrigger)
registry.register_input_trigger(tap)
function tap:_init(inputs, args)
    ButtonInputTrigger._init(self, inputs, args)
    self._down = false
    beepio_bus.add_handler('button.down', 3, bind(self, 'on_button_down'))
    beepio_bus.add_handler('button.up', 3, bind(self, 'on_button_up'))
end

function tap:on_button_down(time, button)
    if button ~= self.input then
        return
    end
    self._down = true
end

function tap:on_button_up(time, button)
    if button ~= self.input then
        return
    end
    if self._down then
        beepio_bus.send_event('tap', self)
    end
    self._down = false
end

-- counts downward presses within a predefined interval of eachother. Each
-- downward press must be within Nms of the previous. But we don't send the
-- multitap event til the final release in the sequence.
class('multitap', ButtonInputTrigger)
registry.register_input_trigger(multitap)
function multitap:_init(inputs, args)
    ButtonInputTrigger._init(self, inputs, args)
    self._count = 0
    self._tap_trigger = nil
    self._timer = nil
    self._sending_tap = false
    self._down = false
    beepio_bus.add_handler('button.down', 3, bind(self, 'on_button_down'))
    beepio_bus.add_handler('button.up', 3, bind(self, 'on_button_up'))
    beepio_bus.add_handler('tap', 4, bind(self, 'on_button_tap'))
end

function multitap:send_event()
    log:info('Multitap count (%s): %s', self.input.__name, self._count)
    if self._count == 1 then
        -- send the tap event, guarded so we don't rehandle it ourselves
        self._sending_tap = true
        beepio_bus.send_event('tap', self._tap_trigger)
        self._sending_tap = false
    else
        beepio_bus.send_event('multitap', self, {count = self._count})
    end
end

function multitap:on_timer_expire()
    self._timer = nil
    if not self._down then
        self:send_event()
    end
end

function multitap:on_button_down(time, button)
    if button ~= self.input then
        return
    end
    self._down = true
    if not self._timer then
        self._count = 1
        self._timer = uloop.timer(bind(self, 'on_timer_expire'), 340)
    else
        self._count = self._count + 1
        if self._count < 3 then
            self._timer:set(340)
        end
    end
end

function multitap:on_button_up(time, button)
    if button ~= self.input then
        return
    end
    self._down = false
    if not self._timer then
        self:send_event()
    end
end

function multitap:on_button_tap(time, trigger)
    if trigger.input ~= self.input then
        return
    end
    self._tap_trigger = trigger
    if self._sending_tap then
        return
    end
    -- stop propagation of tap event, we'll resend it if we detect that this
    -- is not a multitap.
    return true
end

class('hold', ButtonInputTrigger)
registry.register_input_trigger(hold)
function hold:_init(inputs, args)
    ButtonInputTrigger._init(self, inputs, args)
    self._start_time = 0
    self._sent_interval = 0
    self._hold_handled = false
    beepio_bus.add_handler('button.down', 2, bind(self, 'on_button_down'))
    beepio_bus.add_handler('button.up', 2, bind(self, 'on_button_up'))
end

function hold:on_button_down(time, button)
    if button ~= self.input then
        return
    end
    self._sent_interval = 0
    self._start_time = time
    self._hold_handled = false
    self._timer = uloop.timer(bind(self, 'on_timer_expire'), 100)
end

function hold:on_button_up(time, button)
    if button ~= self.input then
        return
    end
    self._timer:cancel()
    if self._hold_handled then
        -- if something handled this hold event cancel the button up event
        -- so that others don't process it as a tap
        return true
    end
end

function hold:on_timer_expire()
    local elapsed = beepio_clock.millis() - self._start_time
    local start_interval = self._sent_interval + 100
    if elapsed >= start_interval then
        for i = start_interval, elapsed, 100 do
            if beepio_bus.send_event('hold', self, {time = i}) then
                self._hold_handled = true
            end
            self._sent_interval = i
        end
    end
    self._timer = uloop.timer(bind(self, 'on_timer_expire'), 100)
end


-- Triggers an initial event when button is first down, then sends an event
-- every 100ms after an initial 1s period.
class('hold_and_repeat', ButtonInputTrigger)
registry.register_input_trigger(hold_and_repeat)
function hold_and_repeat:_init(inputs, args)
    ButtonInputTrigger._init(self, inputs, args)
    self._start_time = 0
    self._sent_interval = 0
    beepio_bus.add_handler('button.down', 2, bind(self, 'on_button_down'))
    beepio_bus.add_handler('button.up', 2, bind(self, 'on_button_up'))
end

function hold_and_repeat:on_button_down(time, button)
    if button ~= self.input then
        return
    end
    self._sent_interval = 0
    self._start_time = time
    beepio_bus.send_event('hold_and_repeat', self)
    self._timer = uloop.timer(bind(self, 'on_timer_expire'), 100)
end

function hold_and_repeat:on_button_up(time, button)
    if button ~= self.input then
        return
    end
    self._timer:cancel()
    return true
end

function hold_and_repeat:on_timer_expire()
    local elapsed = beepio_clock.millis() - self._start_time
    local start_interval = self._sent_interval + 200

    if elapsed > 1000 and elapsed >= start_interval then
        for i = start_interval, elapsed, 200 do
            beepio_bus.send_event('hold_and_repeat', self)
            self._sent_interval = i
        end
    end
    self._timer = uloop.timer(bind(self, 'on_timer_expire'), 100)
end


class('turn', RotaryInputTrigger)
registry.register_input_trigger(turn)
function turn:_init(inputs, args)
    RotaryInputTrigger._init(self, inputs, args)
    beepio_bus.add_handler('rotary.turn', 1, bind(self, 'on_turn'))
end

function turn:on_turn(time, rotary, delta)
    if rotary ~= self.input then
        return
    end
    beepio_bus.send_event('turn', self, {delta=delta})

    -- TODO: oops, we need to hard code this since we can't pass the delta
    -- through right now
    if beepio_model.local_id then
        ubus_call(beepio_model.ubus_conn, 'beep.head', 'call', {
            context = beepio_model.context,
            object = 'audio',
            method = 'adjust_volume',
            params = {
                players = {
                    [beepio_model.local_id] = 42 * delta
                }}})
    end
end


local trigger_cache = {}

function get_trigger(trigger_name, inputs)
    local key = {trigger_name}
    for i, input in ipairs(inputs) do
        table.insert(key, input.NAME)
    end
    key = table.concat(key, '.')
    if trigger_cache[key] then
        return trigger_cache[key]
    end
    local trigger_class = beepio_registry.input_triggers[trigger_name]
    if not trigger_class then
        log:error('Do not have trigger: %s', trigger_name)
        return nil
    end
    local trigger = trigger_class(inputs)
    if not trigger:check_inputs() then
        return nil
    end
    trigger_cache[key] = trigger
    return trigger
end
