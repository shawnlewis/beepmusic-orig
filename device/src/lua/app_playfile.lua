#!/usr/bin/env lua

require 'ubus'
require 'uloop'

require 'beep_ubus'
require 'util'
local flags = require 'flags'
local log = require 'log'

local on_acquire_done
local on_set_station_done
local on_resume_done
local on_track_begin_done
local on_buffer_done

local arg = flags.init(arg)
log:init('app_playfile')

uloop.init()
local conn = beep_ubus_connect('app_playfile')
local _audio_type = ''

local BLOCK_SIZE = 1024 * 16

if #arg < 1 then
    print('Usage: ./app_playfile [flags] <file>')
    print('  --help for information on optional flags')
    os.exit(1)
end

function on_acquire_done(result)
    if not result then
        log:error('Failed to acquire audio')
        os.exit(1)
    end

    token = result['token']
    log:info('Acquire success! Token = ' .. token)


    ubus_call(conn, 'beep.distributor', 'set_station',
        {token=token,
         station_name='Local File',
         station_image_url='http://beepdevices.com:8091/playfile.png',
         play_station_method='play_station',
         play_station_args={file=filename}},
        on_set_station_done)
end

function on_set_station_done(result)
    if not result then
        log:error('Failed to set station')
        os.exit(1)
    end

    ubus_call(conn, 'beep.distributor', 'resume', nil, on_resume_done)
end

function on_resume_done(result)
    if not result then
        log:error('Failed to resume audio')
        os.exit(1)
    end

    track_info = {
        title0='Local File',
        title1=filename,
        image_url='http://beepdevices.com:8091/playfile.png'
    }

    ubus_call(conn, 'beep.distributor', 'track_begin',
        {token=token, audio_type=_audio_type, track_info=track_info,
         content_length=filesize},
        on_track_begin_done)
end

function on_track_begin_done(result)
    if not result then
        log:error('Failed to start track')
        os.exit(1)
    end

    on_buffer_done(true)
end

local buffer_count = 0
function on_buffer_done(result)
    if not result then
        log:error('Buffering failure!')
        os.exit(1)
    else
        buffer_count = buffer_count + 1
        log:info('buffer count %s', buffer_count)

        -- The first two branches in this if else can be used to cause a
        -- flush followed by more buffering and an exit.
        if buffer_count == -1 then
            os.execute('sleep 2')
            ubus_call(conn, 'beep.distributor', 'flush', {token=token},
                    on_buffer_done)
        elseif buffer_count == -1 then
            os.exit(0)
        else
            buf = f:read(BLOCK_SIZE)
            if not buf then
                ubus_call(conn, 'beep.distributor', 'track_end', {token=token})
                log:info('Done buffering.')
                uloop.timer(function()
                    os.exit(0)
                end, 1000)
            else
                ubus_call(conn, 'beep.distributor', 'buffer', {token=token, data=buf},
                        on_buffer_done)
            end
        end
    end
end

function fsize (file)
    local current = file:seek()      -- get current position
    local size = file:seek("end")    -- get file size
    file:seek("set", current)        -- restore position
    return size
end

filename = arg[#arg]
f = io.open(filename, 'r')
filesize = fsize(f)

if not f then
    log:error('File not found: ' .. filename)
    os.exit(1)
end

if string_ends(filename, 'mp3') then
    _audio_type='m'
elseif string_ends(filename, 'wav') then
    _audio_type='p'
elseif string_ends(filename, 'aac') then
    _audio_type='a'
elseif string_ends(filename, 'ogg') then
    _audio_type='o'
elseif string_ends(filename, 'flac') or string_ends(filename, 'flc') then
    _audio_type='f'
else
    log:error('Unknown file format: ' .. filename)
    os.exit(1)
end

log:info('Acquiring audio...')
result = ubus_call(conn, 'beep.distributor', 'acquire',
    {app_ubus_obj='app_playfile'}, on_acquire_done)

uloop.run()
