module(..., package.seeall)

require 'util'
local log = require 'log'

require('beepio_bus')

local M

class('InputAction')
function InputAction:_init(mode, action, trigger, trigger_event, trigger_args)
    self.active_mode = nil
    self.mode = mode
    self.action = action
    self.trigger = trigger
    self.trigger_args = trigger_args
    beepio_bus.add_handler(trigger_event, 9, bind(self, 'handle_trigger'))
end

function InputAction:set_active_mode(active_mode)
    self.active_mode = active_mode
end

function InputAction:handle_trigger(time, target, args)
    if self.trigger ~= target or self.mode ~= self.active_mode then
        return
    end
    local run = true
    for key, val in pairs(self.trigger_args) do
        if not args[key] then
            log:error('InputAction %s requires incorrect arg %s',
                    self.__name, key)
            return
        end
        if tostring(args[key]) ~= val then
            run = false
        end
    end
    if run then
        self:run()
        return true
    end
end

function InputAction:run()
    log:info('InputAction %s %s', self.mode, self.action)
    beepio_bus.send_event('action.' .. self.action, self)
end
