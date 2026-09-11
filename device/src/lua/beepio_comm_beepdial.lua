module(..., package.seeall)

local i2c = require 'i2c'
local log = require 'log'

local clock = require 'beepio_clock'

local I2C_VERSION = nil

local leds = {}
for i = 1, 24 do
    leds[i] = 0
end

local button_callback, knob_callback
local n_recv = 0
local n_ack = 0

local function do_i2c_comm(leds)
    assert(#leds == 24)

    -- flip so 1 is on bottom of device
    local flipped_leds = {}
    for i = 1, 24 do
        flipped_leds[i] = leds[((i + 11) % 24) + 1]
    end

    -- scale
    for i = 1, 24 do
        local val = 255
        for j = 1, 3 do
            val = val * flipped_leds[i] / 255
        end
        flipped_leds[i] = math.floor(val)
    end

    if NO_I2C then
        --log:info('Setting LEDS: %s', leds)
        return 0, 0, 0, 0
    end

    -- Byte 25 is ACK: 0xAA acknowledges previous read, anything else ignored
    -- (we set it to 0x00)
    if n_ack ~= n_recv then
        flipped_leds[25] = 0xaa
        n_ack = n_ack + 1
    else
        flipped_leds[25] = 0x00
    end

    local status = i2c.write(0, 0x23, 0x80, unpack(flipped_leds))

    local knob_delta, button_down_delta, button_up_delta = 0, 0, 0
    local dropped_encoder_frames = 0
    if I2C_VERSION == 0 then
        local success, response = pcall(i2c.read, 0, 0x23, 1, 3)
        if success then
            knob_delta = response[1]

            -- On the old version we just register each press as a down followed
            -- by an up.
            button_down_delta = response[2]
            button_up_delta = response[2]
        else
            --TODO: don't log this too frequently.
            --log:error('no i2c response')
        end
    else
        local success, response = pcall(i2c.read_and_verify1, 0, 0x23, 1, 4)
        if success then
            knob_delta = response[1]
            button_down_delta = response[2]
            button_up_delta = response[3]
            dropped_encoder_frames = response[4]
            n_recv = n_recv + 1
        end
    end
    if knob_delta > 127 then
        -- it's negative, so twos complement
        knob_delta = -1 * (256 - knob_delta)
    end
    return knob_delta, button_down_delta, button_up_delta,
            dropped_encoder_frames
end

local function on_tick()
    local knob_delta, button_down_delta, button_up_delta,
            dropped_encoder_frames = do_i2c_comm(leds)
    if knob_callback and knob_delta ~= 0 then
        knob_callback(knob_delta)
    end
    if button_callback and (button_up_delta ~= 0 or buttown_down_delta ~=0) then
        button_callback(button_down_delta, button_up_delta)
    end
    if dropped_encoder_frames ~= 0 then
        log:error('Got dropped_encoder_frames: ' .. dropped_encoder_frames)
    end
end

local function read_i2c_version()
    local success, response = pcall(i2c.read, 0, 0x23, 0, 3)
    if success then
        return true, response[3]
    else
        return false
    end
end

local function establish_i2c_version()
    local success
    local version
    for i = 1, 3 do
        success, version = read_i2c_version()
        if success then
            break
        end
    end
    if not success then
        log:error('Couldn not establish i2c version')
        os.exit(1)
    end
    I2C_VERSION = version
    log:info('i2c version: ' .. I2C_VERSION)
end

function set_button_callback(cb)
    button_callback = cb
end

function set_knob_callback(cb)
    knob_callback = cb
end

function set_all_leds(val)
    for i = 1, 24 do
        leds[i] = val
    end
end

function set_led(n, val)
    leds[n] = val
end

local inited = false
function init()
    if not inited then
        establish_i2c_version()
        clock.add_tick_callback(on_tick, 24)
    end
    inited = true
end
