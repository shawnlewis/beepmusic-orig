module(..., package.seeall)

require 'util'

local log = require 'log'
local trigint = require 'trigint'

require('beepio_bus')
require('beepio_clock')
require('beepio_model')
require('beepio_registry')

class('OutputView')
function OutputView:_init(outputs, args)
    self.args = args
    if #outputs == 1 then
        self.output = outputs[1]
    else
        self.outputs = outputs
    end
end

function OutputView:check()
    -- TODO: check args
    local outputs, output_types
    if self.output then
        outputs = {self.output}
        output_types = {self.OUTPUT_TYPE}
    else
        outputs = self.outputs
        output_types = self.OUTPUT_TYPES
    end
    local result = self:_check_outputs(outputs, output_types)
    if not result then
        log:error('OutputView %s expects output types %s but got outputs %s',
                self.__name, output_types, outputs)
    end
    return result
end

function OutputView:_check_outputs(outputs, output_types)
    if #outputs ~= #output_types then
        return false
    end
    for i = 1, #outputs do
        if outputs[i].TYPE ~= output_types[i] then
            return false
        end
    end
    return true
end


-- Starts and stops a list of subviews at the same time. Only used internally.

class('OutputViewGroup')
function OutputViewGroup:_init()
    self.views = {}
end

function OutputViewGroup:add_view(view)
    table.insert(self.views, view)
end

function OutputViewGroup:start()
    for i, view in ipairs(self.views) do
        view:start()
    end
end

function OutputViewGroup:stop()
    for i, view in ipairs(self.views) do
        view:stop()
    end
end


-----
-- High-level outputs
-----

class('LedRing24OutputView', OutputView)
LedRing24OutputView.OUTPUT_TYPE = 'led_ring24'


class('LedOutputView', OutputView)
LedOutputView.OUTPUT_TYPE = 'led'

class('RGBLedOutputView', OutputView)
RGBLedOutputView.OUTPUT_TYPE = 'rgb_led'

-----
-- LedRing24 outputs
-----

--class('spin', LedRing24OutputView)
--beepio_registry.register_view(spin)
--
--function spin:start()
--    self._on_led = 1
--    self._counter = 0
--    self._tick_callback = bind(self, 'on_tick')
--    beepio_clock.add_tick_callback(self._tick_callback)
--end
--
--function spin:stop()
--    beepio_clock.remove_tick_callback(self._tick_callback)
--end
--
--function spin:on_tick()
--    self._on_led = self._on_led + self._counter
--    self._on_led = self._on_led % 24 + 1
--    self._counter = self._counter + 7
--    --if self._on_led == 25 then
--    --    self._on_led = 1
--    --end
--    self.output:set_all_leds(0)
--    self.output:set_led(self._on_led, 255)
--end

class('double_accel_spin', LedRing24OutputView)
beepio_registry.register_view(double_accel_spin)
function double_accel_spin:_init(...)
    OutputView._init(self, ...)
    self._start_time = beepio_clock.millis()
    self._on_led1 = 1
    self._on_led2 = 1
    self._led1_speed = 1
    self._led2_speed = 1
end

function double_accel_spin:start()
    self._tick_callback = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._tick_callback)
end

function double_accel_spin:stop()
    beepio_clock.remove_tick_callback(self._tick_callback)
end

function double_accel_spin:on_tick()
    self._on_led1 = self._on_led1 + self._led1_speed
    self._on_led1 = self._on_led1 % 24
    if self._on_led1 == 0 then
        self._on_led1 = 24
    end
    self._on_led2 = self._on_led2 - self._led2_speed
    self._on_led2 = self._on_led2 % 24 + 1
    if self._on_led2 == 0 then
        self._on_led2 = 24
    end

    self._led1_speed = math.floor((beepio_clock.millis() - self._start_time) / 1000)
    self._led2_speed = math.floor((beepio_clock.millis() - self._start_time) / 1000)
    self.output:set_all_leds(0)
    self.output:set_led(self._on_led1, 255)
    self.output:set_led(self._on_led2, 255)
end

class('fade', LedRing24OutputView)
beepio_registry.register_view(fade)

function fade:start()
    self._on = false
    self._fade_start = beepio_clock.millis()
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
end

function fade:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function fade:on_tick()
    local millis_since_start = beepio_clock.millis() - self._fade_start
    local offset = millis_since_start % 5000

    -- For some reason we get a flicker if we go over 250. (should be able
    -- to go to 255)
    local brightness

    if offset < 2500 then
        brightness = 50 + math.floor(200 * offset / 2500)
    else
        brightness = 50 + math.floor(200 * (5000 - offset) / 2500)
    end


    if millis_since_start >= 12500 then
        -- dim_factor 0 at 12.5s, up to 10 at 13.5s (12.5s is the brightness
        -- apex of the 3rd cycle)
        local dim_factor = (millis_since_start - 12500) / 100
        if dim_factor > 10 then
            dim_factor = 10
        end

        brightness = (20 * dim_factor / 10)
                + (brightness * ((17 - dim_factor) / 17))
        self.output:set_all_leds(brightness)

        for i = 0, dim_factor, 1 do
            self.output:set_led(13 + i, 0)
            self.output:set_led(12 - i, 0)
        end
    else
        self.output:set_all_leds(brightness)
    end

end

class('show_volume', LedRing24OutputView)
beepio_registry.register_view(show_volume)
function show_volume:_init(...)
    OutputView._init(self, ...)
    self._handler = bind(self, 'show')
end

function show_volume:start()
    beepio_bus.add_handler('system.volume_change', 1, self._handler)
    self:show()
end

function show_volume:stop()
    beepio_bus.remove_handler(self._handler)
end

function show_volume:show()
    local volume = beepio_model.system:get('local_volume')
    local on_leds = math.floor(24 * volume / 1000)
    for i = 1, 24 do
        if i <= on_leds then
            self.output:set_led(i, 255)
        else
            self.output:set_led(i, 0)
        end
    end
end

class('mergesplit', LedRing24OutputView)
beepio_registry.register_view(mergesplit)

function mergesplit:start()
    self._mergesplit_start = beepio_clock.millis()
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
    self._nodes = {{1, 1.2}}
end

function mergesplit:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function mergesplit:move()
    for i, node in ipairs(self._nodes) do
        node[1] = node[1] + node[2]
        if node[1] >= 25 then
            node[1] = node[1] - 24
        end
        if node[1] < 1 then
            node[1] = 24 - (1 - node[1])
        end
    end
end

function mergesplit:merge_split()
    -- merge
    for i, node1 in ipairs(self._nodes) do
        for j, node2 in ipairs(self._nodes) do
            if i ~= j and math.abs(node1[1] - node2[1]) < 0.5 and math.random() < .1 then
                log:debug('Removing node: %s', i)
                table.remove(self._nodes, i)
            end
        end
    end

    -- split
    for i, node in ipairs(self._nodes) do
        if math.random() < 0.01 then
            table.insert(self._nodes, {node[1], -node[2]})
            log:debug('Adding node')
        end
    end
end

function mergesplit:on_tick()
    self:move()
    self:merge_split()
    local offset = (beepio_clock.millis() - self._mergesplit_start) % 5000

    self.output:set_all_leds(0)
    for i, node in ipairs(self._nodes) do
        self.output:set_led(math.floor(node[1]), 255)
    end
end

-- double bounce from bottom animation


class('twinkle', LedRing24OutputView)
beepio_registry.register_view(twinkle)

function twinkle:start()
    self._twinkle_start = beepio_clock.millis()
    self._tick_count = 0
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
    self._twinkle_table = {}
end

function twinkle:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function twinkle:on_tick()
    self._tick_count = self._tick_count + 1
    self.output:set_all_leds(0)

    -- render
    for led, twinkle_state in pairs(self._twinkle_table) do
        local twinkle_time = beepio_clock.millis() - twinkle_state['start']
        if twinkle_time > twinkle_state['length'] then
            self._twinkle_table[led] = nil
        else
            local on_time = twinkle_state['length'] / 4
            local off_time = twinkle_state['length'] * 3 / 4
            if twinkle_time < on_time then
                -- ramp up for first 1/4 of time
                self.output:set_led(led,
                        math.floor(255 * twinkle_time / on_time))
            else
                -- ramp down for the rest of the time
                self.output:set_led(led,
                        math.floor(255 * (twinkle_state['length'] - twinkle_time)
                                / off_time))
            end
        end
    end

    -- generate new twinkling LED every other tick.
    if self._tick_count % 2 == 0 then
        local twinkle_led = math.random(24)
        if not self._twinkle_table[twinkle_led] then
            self._twinkle_table[twinkle_led] =
                {start= beepio_clock.millis(),
                 length= math.random(250, 1000)}
        end
    end
end


class('spokes', LedRing24OutputView)
beepio_registry.register_view(spokes)

function spokes:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.n = tonumber(args.count)
    self.speed = tonumber(args.speed)
end

function spokes:start()
    self._cb = bind(self, 'on_tick')
    self._head_led = 1
    beepio_clock.add_tick_callback(self._cb)
end

function spokes:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function spokes:on_tick()
    self.output:set_all_leds(0)
    self._head_led = self._head_led + self.speed

    --leds[((head_led - 2) % n_leds) + 1] = 128
    --leds[((head_led - 1) % n_leds) + 1] = 192
    for i = 0, 23, (24 / self.n) do
        self.output:set_led(math.floor(self._head_led + i) % 24 + 1, 255)
    end
end

class('ripple', LedRing24OutputView)
beepio_registry.register_view(ripple)

function ripple:_init(outputs, args)
    OutputView._init(self, outputs, args)
    --self.n = tonumber(args.count)
    --self.speed = tonumber(args.speed)
end

function ripple:start()
    self._cb = bind(self, 'on_tick')
    --self._head_led = 1
    beepio_clock.add_tick_callback(self._cb)
end

function ripple:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function ripple:on_tick()
    for i = 1, 24 do
        local led_brightness = math.cos(
            (beepio_clock.millis() / 1000 + (i-1)/2.0) * math.pi / 2)
        --local led_brightness = math.cos((i % 4) / 4 * math.pi / 4 +  beepio_clock.millis() / 1000) * 255
        self.output:set_led(i, math.floor(math.abs(led_brightness) * 255))
    end
end

class('all_on', LedRing24OutputView)
beepio_registry.register_view(all_on)

function all_on:start()
    self.output:set_all_leds(255)
end

function all_on:stop()
end

class('nick', LedRing24OutputView)
beepio_registry.register_view(nick)

function nick:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.period = tonumber(args.period_ms)
end

function nick:start()
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
end

function nick:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function nick:on_tick()
    self.output:set_all_leds(0)
    local x = 256 * (beepio_clock.millis() % self.period) / self.period
    local pos = 6 * ((trigint.sin(math.floor(x)) / 128) + 1)
    local row = math.floor(pos) % 12 + 1
    self.output:set_led(row, 255)
    self.output:set_led(25 - row, 255)
end

class('ultrasmiley', LedRing24OutputView)
beepio_registry.register_view(ultrasmiley)

function ultrasmiley:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.period = tonumber(args.period_ms)
    self.offset = tonumber(args.offset)
    self.bias = tonumber(args.bias)
end

function ultrasmiley:start()
    self._cb = bind(self, 'on_tick')
    self._pos = self.offset
    beepio_clock.add_tick_callback(self._cb)
end

function ultrasmiley:stop()
    beepio_clock.remove_tick_callback(self._cb)
end

function ultrasmiley:on_tick()
    local smiley = {3, 9, 10, 11, 12, 13, 14, 15, 16, 22}

    local speed = 256 * (beepio_clock.millis() % self.period) / self.period

    self.output:set_all_leds(0)

    local d_pos = self.bias + (trigint.sin(math.floor(speed)) / 256)

    self._pos = self._pos + d_pos

    for i,x in ipairs(smiley) do
        self.output:set_led((x + math.floor(self._pos)) % 24 + 1, 255)
    end
end


class('ring_blink', LedRing24OutputView)
beepio_registry.register_view(ring_blink)

function ring_blink:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.period = tonumber(args.period_ms)
end

function ring_blink:start()
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
end

function ring_blink:stop()
    self.output:set_all_leds(0)
    beepio_clock.remove_tick_callback(self._cb)
end

function ring_blink:on_tick()
    local is_on = (beepio_clock.millis() % self.period) < (self.period / 2)
    if is_on then
        self.output:set_all_leds(255)
    else
        self.output:set_all_leds(0)
    end
end


class('on', LedOutputView)
beepio_registry.register_view(on)

function on:start()
    self.output:set(1000)
end

function on:stop()
    self.output:set(0)
end


class('blink', LedOutputView)
beepio_registry.register_view(blink)

function blink:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.period = tonumber(args.period_ms)
end

function blink:start()
    self._cb = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._cb)
end

function blink:stop()
    self.output:set(0)
    beepio_clock.remove_tick_callback(self._cb)
end

function blink:on_tick()
    local is_on = (beepio_clock.millis() % self.period) < (self.period / 2)
    if is_on then
        self.output:set(1000)
    else
        self.output:set(0)
    end
end

class('set_rgb', RGBLedOutputView)
beepio_registry.register_view(set_rgb)

function set_rgb:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.r = tonumber(args.r)
    self.g = tonumber(args.g)
    self.b = tonumber(args.b)
end

function set_rgb:start()
    self.output:set(self.r, self.g, self.b)
end

function set_rgb:stop()
    self.output:set(0, 0, 0)
end

class('random_walk', RGBLedOutputView)
beepio_registry.register_view(random_walk)

local MAX_RGB_VAL = 120
function random_walk:_init(outputs, args)
    OutputView._init(self, outputs, args)
    self.speed_mult = tonumber(args.speed)
    self.val = {0, 0, 0}
    self.target = {}
    self.targets = {
        {60, 0, 0},
        {0, 60, 0},
        {0, 0, 120}}
        --{120, 120, 0},
        --{120, 0, 120},
        --{0, 120, 120}}
        --{120, 60, 60},
        --{60, 120, 60},
        --{30, 60, 120}}
    self.speed = {}
    self:pick_target()
end

function random_walk:start()
    self._tick_callback = bind(self, 'on_tick')
    beepio_clock.add_tick_callback(self._tick_callback)
end

function random_walk:stop()
    self.output:set(0, 0, 0)
    beepio_clock.remove_tick_callback(self._tick_callback)
end

function random_walk:pick_target()
    local target_index = math.random(1, #self.targets)
    local new_target = self.targets[target_index]

    for i = 1, 3 do
        self.speed[i] = self.speed_mult * (new_target[i] - self.val[i]) / 120
    end
    self.target = new_target
    --for i = 1, 3 do
    --    local max = 120
    --    local val = math.floor(max * math.random())
    --    self.target[i] = math.floor(max * val * val * val / (max * max * max))

    --    if math.random() < .33 then
    --        self.target[i] = 0
    --    end

    --    if self.target[i] < 10 then
    --        self.target[i] = 10
    --    end
    --end
end


function random_walk:on_tick()
    local hit_target = true
    for i = 1, 3 do
        if math.floor(self.val[i]) ~= self.target[i] then
            hit_target = false
        end
    end
    if hit_target then
        self:pick_target()
    end

    for i = 1, 3 do
        self.val[i] = self.val[i] + self.speed[i]
        if self.speed[i] < 0 and self.val[i] < self.target[i] then
            self.val[i] = self.target[i]
        elseif self.speed[i] > 0 and self.val[i] > self.target[i] then
            self.val[i] = self.target[i]
        end
    end

    self.output:set(math.floor(self.val[1]),
                    math.floor(self.val[2]),
                    math.floor(self.val[3]))
end
