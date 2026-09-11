local inspect = require 'inspect'
local log = require 'log'
local apps = require 'beepmanager_apps'

require 'util'

log:init('grouper')

function _sort_descending(x, y)
    return x > y
end

function _sort_signals(x, y)
    if x['signal'] == y['signal'] then
        return x['name'] > y['name']
    else
        return x['signal'] > y['signal']
    end
end

local VERBOSE_ASSIGN = false
function print_trace(str)
    if(VERBOSE_ASSIGN) then
        --log:info(str)
        print(str)
    end
end
-- assign_groups
--   <config> is a table of devices, where each device is itself a table
--   with fields "source", "sink", "signal" and "source_time", and "integrations"
--
--   returns a new config in the same format with properly assigned
--   groupings
function assign_groups(config)
    local new_config = deepcopy(config)

    print_trace('## 0. Original config: ' .. inspect(config))

    -- Remove duplicate sources (keep oldest source time)
    print_trace('## Remove duplicate sources')
    for i,_ in pairs(new_config) do
        for j,_ in pairs(new_config) do
            if i ~= j then
                if new_config[i].source == new_config[j].source then
                    print_trace(' >>> source conflict <<< ' .. i .. ' ' .. j)
                    if new_config[i].source_time > new_config[j].source_time then
                        new_config[i].source = '-1'
                    elseif new_config[i].source_time < new_config[j].source_time then
                        new_config[j].source = '-1'
                    else  -- when equal tie-break with string comparison
                        if i > j then
                            new_config[j].source = '-1'
                        else
                            new_config[i].source = '-1'
                        end
                    end
                end
            end
        end
    end

    print_trace('duplicate sources removed: ' .. inspect(new_config))

    -- List all unique sinks and sources
    print_trace('## List all unique sinks and sources')
    local source_set = {}
    local sink_set = {}
    for _,dev_obj in pairs(new_config) do
        sink_set[dev_obj.sink] = true
        if dev_obj.source ~= '-1' then
            source_set[dev_obj.source] = true
        end
    end

    print_trace('source_set: ' .. inspect(source_set))
    print_trace('sink_set: ' .. inspect(sink_set))

    -- Remove groups from source set that have no sinks and
    -- update table
    print_trace('## Remove groups from source set that have no sinks and update table')
    for source,_ in pairs(deepcopy(source_set)) do
        if sink_set[source] == nil then
            source_set[source] = nil
            for dev, dev_obj in pairs(new_config) do
                if dev_obj.source == source then
                    dev_obj.source = '-1'
                end
            end
        end
    end

    print_trace('culled source_set: ' .. inspect(source_set))

    -- Remove groups from sink set that have sources
    print_trace('## Remove groups from sink set that have sources')
    for source,_ in pairs(source_set) do
        sink_set[source] = nil
    end


    print_trace('culled sink_set: ' .. inspect(sink_set))

    -- Sort groups
    print_trace('## Sort groups with undefined sources')
    local groups = {}
    for i,_ in pairs(sink_set) do table.insert(groups, i) end
    print_trace('unsorted groups: ' .. inspect(groups))
    table.sort(groups, _sort_descending)
    print_trace('sorted groups: ' .. inspect(groups))

    -- Build device_set
    print_trace('## Build device set')
    local device_set = {}
    for dev,dev_obj in pairs(new_config) do
        local key = {name = dev, signal = dev_obj.signal}
        device_set[key] = true
    end

    print_trace('device_set: ' .. inspect(device_set))

    -- Sort devices
    print_trace('## Sort devices')
    local devices = {}
    for i,_ in pairs(device_set) do table.insert(devices, i) end
    print_trace('unsorted devices: ' .. inspect(devices))
    table.sort(devices, _sort_signals)
    print_trace('sorted devices: ' .. inspect(devices))

    -- Assign integrations:
    --   [Assume 0-base array indexing]
    --   Let I = integrations sorted alphabetically
    --   Let D = devices sorted by signal strength, then name
    --   Clear all existing integrations for each device in D
    --   Let i = 0
    --   For i=0 to length(I):
    --     Let d = i % length(D)
    --     Append I[i] to D[d].integrations
    print_trace('## Assign integrations')
    if #devices > 0 then
        local integrations = apps:get_ordered_integrations()
        local num_devices = #devices

        for _,config in pairs(new_config) do
            config.integrations = {}
        end

        for index,integration in ipairs(integrations) do
            local device_index = ((index - 1) % num_devices) + 1
            local device_name = devices[device_index].name

            table.insert(new_config[device_name].integrations, integration)
        end
    end

    print_trace('integrations assigned: ' .. inspect(new_config))

    -- Remove devices from device set that are sources
    print_trace('## Remove devices from device set that are sources')
    for device,_ in pairs(device_set) do
        if new_config[device['name']].source ~= '-1' then
            device_set[device] = nil
        end
    end

    print_trace('non-source device_set: ' .. inspect(device_set))

    -- Sort devices
    print_trace('## Sort non-source devices')
    do -- scope non_source_devices
        local non_source_devices = {}
        for i,_ in pairs(device_set) do table.insert(non_source_devices, i) end
        print_trace('devices: ' .. inspect(non_source_devices))
        table.sort(non_source_devices, _sort_signals)
        print_trace('sorted non-source devices: ' .. inspect(non_source_devices))

        -- For each remaining group, try to assign groups so sink == source
        print_trace('## For each remaining group, try to assign groups so sink == source')
        local i=1
        while i <= #groups do
            local group = groups[i]
            local found_match = false
            for j,device in ipairs(non_source_devices) do
                local device_name = device['name']
                if new_config[device_name].sink == group and
                    new_config[device_name].source == '-1' then -- Hasn't been assigned yet
                    new_config[device_name].source = group
                    table.remove(devices,j)
                    found_match = true
                    break
                end
            end
            if found_match then
                table.remove(groups, i)
            else
                i = i + 1
            end
        end

        print_trace('non-source devices: ' .. inspect(non_source_devices))
        print_trace('groups: ' .. inspect(groups))

        -- Assign the remaining groups to any available devices
        print_trace('## Assign the remaining groups to any available devices')
        local device_index = 1
        for i,group in ipairs(groups) do
            local dev_id = non_source_devices[device_index]['name']
            new_config[dev_id].source = group
            device_index = device_index + 1
        end
    end
    print_trace('## Output: ' .. inspect(new_config))

    local delta = {}

    for dev_id,dev_obj in pairs(new_config) do
        local old = config[dev_id]
        local new = new_config[dev_id]
        if old.sink ~= new.sink then
            delta[dev_id] = {}
            delta[dev_id].sink = tostring(new.sink)
        end

        if old.source ~= new.source then
            if delta[dev_id] == nil then
                delta[dev_id] = {}
            end
            delta[dev_id].source = tostring(new.source)
        end

        if old.integrations ~= new.integrations then
            if delta[dev_id] == nil then
                delta[dev_id] = {}
            end
            delta[dev_id].integrations = new.integrations
        end
    end

    return new_config, delta
end
