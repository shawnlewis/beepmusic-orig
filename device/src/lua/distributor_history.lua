module(..., package.seeall)

require 'beep_ubus'
require 'util'

local beepcloud = require 'beepcloud'
local log = require 'log'
local json = require 'JSON'

HISTORY_SIZE = 10

class('History')
function History:_init(on_ready)
    -- history is an ordered list of stations, from newest to oldest
    self.history = {}
    beepcloud.get_global('history', function(result)
        log:warn('HISTORY RESULT: %s', result)
        if result then
            self.history = json:decode(result)
        else
            beepcloud.set_global('history', json:encode({}))
        end
        if on_ready then
            on_ready()
        end
    end)
end

function History:add(station)
    local station = deepcopy(station)
    local station_id = station.id
    if not station_id then
        log:warn('Station had no id')
        return
    end

    -- If we already have a station with this id in the history, move that
    -- station to the front. Otherwise this is a new station and we just
    -- insert it.
    local new_history = {}
    local found_station = nil
    for _, station in ipairs(self.history) do
        if station.id == station_id then
            if not found_station then
                found_station = station
            end
        else
            table.insert(new_history, station)
        end
    end
    if found_station then
        table.insert(new_history, 1, found_station)
    else
        table.insert(new_history, 1, station)
    end

    self.history = new_history
    while #self.history > HISTORY_SIZE do
        table.remove(self.history)
    end
    beepcloud.set_global('history', json:encode(self.history))
end

function History:at(index)
    return self.history[index]
end

function History:count()
    return #self.history
end

function History:get_list()
    local list = {}
    for i, station in ipairs(self.history) do
        table.insert(list, {
            index = i,
            name = station.name,
            app = station.app
        })
    end
    return list
end
