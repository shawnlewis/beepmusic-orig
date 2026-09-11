module(..., package.seeall)

local config = require 'config'
local log = require 'log'

require 'beepio_registry'
require 'beepio_input'
require 'beepio_input_action'
require 'beepio_input_triggers'
require 'beepio_output'
require 'beepio_output_views'


-- returns a sub-table of the input table, returning only those keys that
-- are prefixed by prefix. The prefix is stripped in the result.
local function subtable_from_prefix(t, prefix)
    local result = {}
    for key, val in pairs(t) do
        if string_starts(key, prefix) then
            local arg_name = string.sub(key, string.len(prefix) + 1, #key)
            result[arg_name] = val
        end
    end
    return result
end

local function require_keys(section, keys)
    local missing = {}
    for i, required_key in ipairs(keys) do
        local is_missing = true
        for key, val in pairs(section) do
            if key == required_key then
                is_missing = false
            end
        end
        if is_missing then
            table.insert(missing, required_key)
        end
    end
    if #missing ~= 0 then
        log:error('Section %s missing keys: %s', section, missing)
        return false
    end
    return true
end

local function create_input(section)
    if not require_keys(section, {'type'}) then
        return nil
    end
    local input = beepio_registry.inputs[section['type']]
    if not input then
        log:error('Input type does not exist: %s', section['type'])
        return nil
    end

    local input_args = subtable_from_prefix(section, section['type'] .. '_')

    return input(section['.name'], input_args)
end

local function create_input_action(inputs, section)
    if not require_keys(section, {'mode', 'trigger', 'input', 'action'}) then
        return nil
    end

    -- get each input into an array
    local input_names = section['input']
    if type(input_names) ~= 'table' then
        input_names = {input_names}
    end
    local required_inputs = {}
    for _, input_name in ipairs(input_names) do
        local input = inputs[input_name]
        if not input then
            log:error('input_action %s requested input that does not exist: %s',
                    section, input_name)
            return nil
        end
        table.insert(required_inputs, input)
    end

    local trigger_name = section['trigger']
    local trigger = beepio_input_triggers.get_trigger(
            section['trigger'], required_inputs)
    if not trigger then
        log:error('Could not create trigger for section: %s', section)
        return nil
    end

    local event_args = subtable_from_prefix(section, trigger_name .. '_')
    local action = beepio_input_action.InputAction(
            section['mode'], section['action'], trigger,
            section['trigger'], event_args)

    return action
end

local function create_output(section)
    if not require_keys(section, {'type'}) then
        return nil
    end
    local output = beepio_registry.outputs[section['type']]
    if not output then
        log:error('Output type does not exist: %s', section['type'])
        return nil
    end

    local output_args = subtable_from_prefix(section, section['type'] .. '_')

    return output(section['.name'], output_args)
end

local function create_view(outputs, section)
    if not require_keys(section, {'trigger', 'output', 'view'}) then
        return nil
    end

    -- get each output into an array
    local output_names = section['output']
    if type(output_names) ~= 'table' then
        output_names = {output_names}
    end
    local required_outputs = {}
    for _, output_name in ipairs(output_names) do
        local output = outputs[output_name]
        if not output then
            log:error('output_action %s requested output that does not exist: %s',
                    section, output_name)
            return nil
        end
        table.insert(required_outputs, output)
    end

    local view_args = subtable_from_prefix(section, section['view'] .. '_')

    local view_class = beepio_registry.views[section['view']]
    if not view_class then
        log:error('Section %s requires view which was not found: %s', section,
                section['view'])
        return nil
    end

    local view = view_class(required_outputs, view_args)
    if not view:check() then
        return nil
    end

    return view
end

function load()
    local uci = config.get_cursor()

    local load_error = false

    local inputs = {}
    local res = uci:foreach('io', 'input', function(section)
        local input = create_input(section)
        if not input then
            load_error = true
        else
            inputs[section['.name']] = input
        end
    end)

    local input_actions = {}
    local res = uci:foreach('io', 'input_action', function(section)
        local input_action = create_input_action(inputs, section)
        if not input_action then
            load_error = true
        else
            table.insert(input_actions, input_action)
        end
    end)

    local outputs = {}
    local res = uci:foreach('io', 'output', function(section)
        local output = create_output(section)
        if not output then
            load_error = true
        else
            outputs[section['.name']] = output
        end
    end)

    local output_actions = {}
    local res = uci:foreach('io', 'output_action', function(section)
        local view = create_view(outputs, section)
        if not view then
            load_error = true
        else
            if not output_actions[section['trigger']] then
                output_actions[section['trigger']]
                        = beepio_output_views.OutputViewGroup()
            end
            output_actions[section['trigger']]:add_view(view)
        end
    end)

    if load_error then
        log:error('Config parse error. Exiting...')
        os.exit(1)
    end

    return inputs, input_actions, outputs, output_actions
end
