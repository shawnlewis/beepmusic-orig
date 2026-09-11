module(..., package.seeall)

local log = require 'log'

require 'gpio'

require 'beepio_bus'
require 'beepio_clock'
require 'beepio_comm_gpio'

local I2C_ADDRESS = 0x14

local I2C_INTERRUPT_GPIO = 27

local I2C_SEQUENCE_RESET_REGISTER = 0xf0
local I2C_SYSINFO_REGISTER = 0x1
local I2C_BEEP_MODE_REGISTER = 0x83

local I2C_INPUT_REGISTER = 0x2
local I2C_INPUT_ACK_REGISTER = 0x80

local I2C_OUTPUT_REGISTER = 0x82
local I2C_OUTPUT_ACK_REGISTER = 0x0

local MODULE_MODE_NORMAL = 0
local MODULE_MODE_UPDATING = 1
local MODULE_MODE_SETUP = 2

local module_mode

local should_start = false

local sysinfo

-- keyed by input_id, values are {type = <type>, callback = <callback>}
-- input ids are guaranteed to be sequential starting at 1, so it can be
-- iterated in order using ipairs, which will return the input id as the
-- first argument
local inputs = {}

local input_expected_seq = 0
local invalid_input_seq_count = 0

local total_read_bytes = 0
local read_timer
local sequential_reads = 0

function enable_input(i2c_id, input_type, change_callback)
    should_start = true
    inputs[i2c_id + 1] = {type = input_type, callback = change_callback}
    if input_type == 'button' then
        total_read_bytes = total_read_bytes + 2
    elseif input_type == 'power_state' then
        total_read_bytes = total_read_bytes + 1
    end
end

function process_inputs(input_vals)
    -- start at 2 to skip the sequence byte.
    local offset = 2
    for _, input in ipairs(inputs) do
        if input.type == 'button' then
            input.callback(input_vals[offset], input_vals[offset + 1])
            offset = offset + 2
        elseif input.type == 'power_state' then
            input.callback(input_vals[offset])
            offset = offset + 1
        end
    end
end

function do_read()
    sequential_reads = sequential_reads + 1
    if sequential_reads > 20 then
        log:error('We\'ve read from i2c too many times in a row? ' ..
                  'Hardware issue? Exiting...')
        os.exit(1)
    end

    local success, response = pcall(
            i2c.read_and_verify2, 0, I2C_ADDRESS, I2C_INPUT_REGISTER,
            total_read_bytes + 1)

    if success then
        log:info('I2C READ SUCCESS: %s % %s ', I2C_ADDRESS, I2C_INPUT_REGISTER, response)
        local seq = response[1]

        if seq ~= input_expected_seq then
            if (seq == 255 and input_expected_seq ~= 0
                    or seq + 1 ~= input_expected_seq) then
                log:error('Got unexpected sequence number: %s', seq)
                os.exit(1)
            end

            -- at this point we know we've received a message that we
            -- previously tried to ack. so just ack it again

            -- track how many times this happens
            invalid_input_seq_count = invalid_input_seq_count + 1
            if invalid_input_seq_count == 5 then
                log:warn('Got 5 invalid input seq in a row')
            end

            -- we need to ack again
            local status = i2c.write(
                    0, I2C_ADDRESS , I2C_INPUT_ACK_REGISTER, 0x0b, 0xaa)
            log:info('ACKING AGAIN')
            if status ~= 0 then
                log:error('Got bad i2c write status: %s', status)
            end
        else
            input_expected_seq = (input_expected_seq + 1) % 256
            invalid_input_seq_count = 0
            process_inputs(response)

            local status = i2c.write(
                    0, I2C_ADDRESS , I2C_INPUT_ACK_REGISTER, 0x0b, 0xaa)
            log:info('ACK')
            if status ~= 0 then
                log:error('Got bad i2c write status: %s', status)
            end
        end
    else
        log:warn('I2C WRITE FAILURE')
    end

    -- schedule another read on the next pass of the loop, so that if
    -- the interrupt gpio goes down we can cancel reading
    read_timer = uloop.timer(do_read, 10)
end

local outputs = {}
local output_needs_commit = false
local output_seq = 0

function enable_output(i2c_id, output_type)
    should_start = true
    if output_type == 'led' then
        outputs[i2c_id + 1] = {type = output_type, val = 0}
    end
    if output_type == 'rgb_led' then
        outputs[i2c_id + 1] = {type = output_type, val = {r = 0, g = 0, b = 0}}
    end
end

function set_led(i2c_id, val)
    output_needs_commit = true
    outputs[i2c_id + 1].val = val
end

function set_rgb_led(i2c_id, r, g, b)
    output_needs_commit = true
    outputs[i2c_id + 1].val = {r = r, g = g, b = b}
end

function i2c_write_and_verify(register, data)
    local message = {output_seq}
    for _, val in ipairs(data) do
        table.insert(message, val)
    end

    local tries = 0
    while true do
        local status = i2c.write_with_checksum(
                0, I2C_ADDRESS , register, unpack(message))
        if status == 0 then
            local success, response = pcall(
                    i2c.read, 0, I2C_ADDRESS, I2C_OUTPUT_ACK_REGISTER, 2)
            if not success then
                log:warn('i2c output ack read failed, retrying.')
            elseif response[1] == 0xe4 and response[2] == 0xaa then
                break
            else
                log:info('Got output !ack response: %s', response)
            end
        else
            log:warn('i2c write and verify failed, retrying')
        end

        tries = tries + 1
        if tries == 10 then
            log:error('Retried i2c output too many times. Exiting')
            os.exit(1)
        end
    end
    output_seq = (output_seq + 1) % 256
end

function commit_output()
    if not output_needs_commit then
        return
    end

    output_needs_commit = false

    local data = {}
    for _, output in ipairs(outputs) do
        if output.type == 'led' then
            table.insert(data, output.val)
        elseif output.type == 'rgb_led' then
            table.insert(data, output.val.r)
            table.insert(data, output.val.g)
            table.insert(data, output.val.b)
        end
    end

    i2c_write_and_verify(I2C_OUTPUT_REGISTER, data)
end

local function send_mode(mode)
    if module_mode ~= mode then
        module_mode = mode
        log:info('sending mode')
        i2c_write_and_verify(I2C_BEEP_MODE_REGISTER, {mode})
    end
end

local function on_mode_starting()
    send_mode(MODULE_MODE_NORMAL)
end

local function on_mode_audio()
    send_mode(MODULE_MODE_NORMAL)
end

local function on_mode_wifisetup()
    send_mode(MODULE_MODE_SETUP)
end

local function on_mode_updating()
    send_mode(MODULE_MODE_UPDATING)
end

function start()
    if should_start then
        local i = 1
        while true do
            if not inputs[i] then
                break
            end
            i = i + 1
        end
        if table_length(inputs) ~= i - 1 then
            log:error('Invalid i2c inputs. Inputs must have contiguous ids '
                    .. 'from 1 to N, where N is the total number of inputs')
            os.exit(1)
        end

        local i = 1
        while true do
            if not outputs[i] then
                break
            end
            i = i + 1
        end
        if table_length(outputs) ~= i - 1 then
            log:error('Invalid i2c outputs. outputs must have contiguous ids '
                    .. 'from 1 to N, where N is the total number of outputs')
            os.exit(1)
        end

        log:info('OUTPUTS %s', outputs)

        -- fetch system info
        for i = 1, 5 do
            local success, response = pcall(
                    i2c.read_and_verify2, 0, I2C_ADDRESS, I2C_SYSINFO_REGISTER,
                    12)
            if success then
                sysinfo = response
                break
            end
        end
        if not sysinfo then
            log:error('Could not fetch i2c system info, exiting...')
            os.exit(1)
        end

        -- reset the microcontroller's sequence counter
        for i = 1, 5 do
            -- let's play ball!
            local status = i2c.write(
                    0, I2C_ADDRESS, I2C_SEQUENCE_RESET_REGISTER,
                    0xba, 0x5e, 0xba, 0x11)
            if status ~= 0 then
                log:error('Got bad i2c write status for seq reset: %s', status)
            end
        end

        beepio_comm_gpio.enable_input(I2C_INTERRUPT_GPIO, false,
            function(is_high)
                log:info('INTERRUPT LINE: %s', is_high)
                if is_high then
                    if not read_timer then
                        sequential_reads = 0
                        do_read()
                    end
                else
                    if read_timer then
                        read_timer:cancel()
                        read_timer = nil
                    end
                end
            end)

        beepio_clock.add_tick_callback(commit_output)

        beepio_bus.add_handler('mode.starting', 1, on_mode_starting)
        beepio_bus.add_handler('mode.audio', 1, on_mode_audio)
        beepio_bus.add_handler('mode.wifisetup', 1, on_mode_wifisetup)
        beepio_bus.add_handler('mode.updating', 1, on_mode_updating)
    end
end

