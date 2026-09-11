require 'beepmanager_grouper'
local inspect = require 'inspect'

local config = {
    devA = {
        source='1',
        sink='3',
        signal=-1,
        source_time=1000,
        integrations={}
    },
    devC = {
        source='2',
        sink='2',
        signal=-1,
        source_time=0,
        integrations={}
    },
    devB = {
        source='3',
        sink='1',
        signal=-2,
        source_time=1000,
        integrations={}
    },
}

new_config, delta = assign_groups(config)
print('NEW CONFIG ==> \n' .. inspect(new_config))
print('DELTA ==> \n' .. inspect(delta))
