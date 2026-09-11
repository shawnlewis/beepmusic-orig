local inspect = require 'inspect'
local log = require 'log'

require 'beep'
require 'uloop'
require 'util'

local DOWN_TIME_THRESH = 30

log:init('netcheck')

-- returns nil if no gateway
function get_gateway()
    local output = get_command_output('route -n | grep ^0.0.0.0 | awk \'{print $2}\'')
    if not output then
        return nil
    end
    local result = string.gsub(output, '\n', '')
    if result == '' then
        return nil
    else
        return result
    end
end

-- returns nil if no gateway
function get_gateway_mac()
    local route_output = get_command_output('route -n')
    local route_lines = string_split_by(route_output, '\n')
    local gateway_route_line = nil

    for _, line in ipairs(route_lines) do
        if string_starts(line, '0.0.0.0') then
            gateway_route_line = line
            break
        end
    end

    if not gateway_route_line then
        log:warn('Default gateway route not found')
        return nil
    end

    local gateway_route_line_items = string_split(gateway_route_line)
    local gateway_ip = gateway_route_line_items[2]

    if not gateway_ip then
        log:warn('Gateway IP not found')
        return nil
    end

    log:info('Got gateway ip: %s', gateway_ip)

    local arp_output = get_command_output('cat /proc/net/arp')
    local arp_lines = string_split_by(arp_output, '\n')
    local gateway_arp_line = nil

    for _, line in ipairs(arp_lines) do
        local fields = string_split(line)
        if gateway_ip == fields[1] then
            gateway_arp_line = line
            break
        end
    end

    if not gateway_arp_line then
        log:warn('Gateway ARP entry not found')
        return nil
    end

    local gateway_arp_line_items = string_split(gateway_arp_line)
    local gateway_mac = gateway_arp_line_items[4]

    if not gateway_mac then
        log:warn('Gateway MAC not found')
    end

    log:info('Got gateway MAC: %s', gateway_mac)

    return gateway_mac

    -- In plain english:
    -- ARP (no lookup) piped through
    -- GREP for...
    --      ROUTE (no lookup) piped through
    --      GREP 0.0.0.0 at beginning of line piped through
    --      AWK (print 2nd item) ...piped through
    -- AWK (print 3rd item)

    -- local output = get_command_output(
    --         'cat /proc/net/arp | ' ..
    --         'grep ` ' ..
    --             'route -n | ' ..
    --             'grep ^0.0.0.0 | ' ..
    --             'awk \'{ print $2 }\' ` | ' ..
    --         'awk \'{ print $4 }\'')

    -- if not output then
    --     return nil
    -- end
    -- local result = string.gsub(output, '\n', '')
    -- if result == '' then
    --     return nil
    -- else
    --     return result
    -- end
end

-- pings host, when done calls callback with true on success or false on failure.
local ping_seq = 0
function ping(host, timeout, callback)
    if not host then
        callback(false)
        return
    end
    local ping_start_h, ping_start_l = beep.beep_millis()
    uloop.process('/bin/ping',
        {'-c', '1', '-W', tostring(timeout), host},
        {}, '/dev/null',
        function(ret)
            local ping_end_h, ping_end_l = beep.beep_millis()
            local _, time_delta = beep.beep_millis_sub(
                    ping_end_h, ping_end_l,
                    ping_start_h, ping_start_l)

            -- make sure the log dupe detection doesn't suppress this message,
            -- by changing the format string every time.
            local format_s = string.format('Ping #%s time:', ping_seq) .. ' %s'
            ping_seq = ping_seq + 1

            if (ping_seq % 30) == 1 then
                log:debug(format_s, time_delta)
            end

            if ret == 0 then
                callback(true)
            else
                callback(false)
            end
        end)
end


local is_down = true
local last_up_time = 0

function ping_loop(netdown_cb, netup_cb, stop_once_up)
    local gateway = get_gateway()
    --log:debug('netcheck gateway: %s', gateway)
    ping(gateway, 5, function(success)
        local time = os.time()
        --log:info('ping success: %s, time: %s, last_up: %s', success, time, last_up_time)
        if is_down and success then
            netup_cb()
            is_down = false
            last_up_time = time
            if stop_once_up then
                return
            end
        elseif not is_down and success then
            last_up_time = time
        elseif not is_down and not success and time - last_up_time >= DOWN_TIME_THRESH then
            netdown_cb()
            is_down = true
        end
        uloop.timer(function()
            ping_loop(netdown_cb, netup_cb, stop_once_up)
        end, 2000)
    end)
end

local M = {}

function M.register(netdown_cb, netup_cb)
    ping_loop(netdown_cb, netup_cb)
end

function M.wait_til_up(netup_cb)
    ping_loop(function() end, netup_cb, true)
end

function M.wait_til_year_set(timeout_s, callback)
    -- NOTE: the timeout check we do relies on the system clock! But as long
    -- as the system clock is not set the timeout check is valid, and when
    -- the system clock is set we are happy and done!
    local start_time = os.time()

    local function check_year()
        local year = os.date('*t')['year']
        if year ~= 1970 then
            log:info('Year is no longer 1970, ready!')
            callback()
        elseif os.time() - start_time > timeout_s then
            log:info('Timed out waiting for year, ready anyway.')
            callback()
        else
            log:debug('System year is not set yet, waiting 1s.')
            uloop.timer(check_year, 1000)
        end
    end

    check_year()
end

return M
