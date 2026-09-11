module(..., package.seeall)

local log = require 'log'

inputs = {}
function register_input(input)
    inputs[input.__name] = input
end

input_triggers = {}
function register_input_trigger(input_trigger)
    input_triggers[input_trigger.__name] = input_trigger
end

outputs = {}
function register_output(output_class)
    outputs[output_class.__name] = output_class
end

views = {}
function register_view(view)
    views[view.__name] = view
end
