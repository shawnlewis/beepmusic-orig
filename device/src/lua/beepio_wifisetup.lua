module(..., package.seeall)

require 'beep_ubus'
local config = require 'config'
local log = require 'log'

require 'beepio_bus'

function should_enter_setup_on_boot()
    local answer = config.data_get('wifisetup_on_boot')
    if not answer or answer == 0 or answer == '0' then
        return false
    end
    return true
end

function setup_is_on()
    local setup_mode_disabled_val = config.get_cursor():get(
            'wireless', 'setup_mode', 'disabled')
    if (not setup_mode_disabled_val) or tonumber(setup_mode_disabled_val) == 0 then
        return true
    else
        return false
    end
end

function disable_setup_mode()
    beepio_bus.send_event('wifisetup', false)
    ubus = get_ubus_conn()
    ubus:call_async('beep.wifisetup', 'shutdown', {}, function() end)
end

function reenable_beep_services()
    beepio_bus.send_event('wifisetup', false)
    os.execute('/etc/init.d/dnsmasq stop')
    os.execute('/etc/init.d/uhttpd_setup stop')

    -- Delete any cores from ramdisk before starting beep services
    os.execute('rm -f /tmp/*.core')

    -- bones is stopped in wifisetup_mode_enable.sh
    os.execute('/etc/init.d/bones start')

    os.execute('/etc/init.d/beepmanager stop')
    os.execute('/etc/init.d/beepmanager start')
end

function enable_setup_mode()
    local iw_params = {}
    table.insert(iw_params, 'wlan0')
    table.insert(iw_params, 'info')

    function check_network()
        uloop.process('/usr/sbin/iw', iw_params, {}, '/dev/null', function(ret)
            if (ret ~= 0) then
                log:info('Network interface not ready')
                uloop.timer(function() check_network() end, 1000)
            else
                log:info('Network interface up!  Entering wifisetup')
                uloop.timer(function()
                    beepio_bus.send_event('wifisetup', true)
                    uloop.process('/bin/ash', {'/beep/wifisetup_mode_enable.sh'},
                            {}, nil, function() end)
                    os.execute('/beep/wifisetup.sh')
                end, 2000)
            end
        end)
    end

    check_network()
end

function add_ubus_object(ubus_objects, ubus_conn)
    ubus_objects['beep.mainio'] = {
        disable_setup = beep_ubus_method(ubus_conn,
            function(req, msg)
                log:debug('disable_setup')
                config.data_set('wifisetup_on_boot', 0)
                reenable_beep_services()
                beep_reply(ubus_conn, req, beep_success())
            end, {__unused = ubus.STRING}
        ),
    }
end

-- on boot, check uci config, turn setup on
-- if ubus disable setup called, disable uci config
-- emit wifisetup state changes when enabling or disabling
