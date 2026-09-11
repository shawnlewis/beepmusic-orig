module(..., package.seeall)

require 'util'
local log = require 'log'

require 'beepio_bus'
require 'beepio_model'

class('Stack')
function Stack:_init()
    self._s = {}
end

function Stack:push(val)
    table.insert(self._s, val)
end

function Stack:pop()
    return table.remove(self._s, #self._s)
end

function Stack:peek(val)
    if #self._s ~= 0 then
        return self._s[#self._s]
    end
end

function Stack:remove(val)
    for i = #self._s, 1, -1 do
        if self._s[i] == val then
            table.concat(i)
            break
        end
    end
    table.remove(self._s)
end

function Stack:count()
    return #self._s
end

function Stack:swap(index, val)
    if index <= #self._s then
        self._s[index] = val
    end
end

class('DummyView')
function DummyView:start()
end
function DummyView:stop()
end

class('ViewStack')
function ViewStack:_init()
    self._stack = Stack()
end

function ViewStack:push(view)
    if not view then
        return
    end
    if self._stack:count() ~= 0 then
        self._stack:peek():stop()
    end
    view:start()
    self._stack:push(view)

    log:info('Current view: %s', self._stack:peek().__name)
end

function ViewStack:pop()
    if not self._stack:peek() then
        return
    end
    self._stack:pop():stop()
    if self._stack:count() ~= 0 then
        self._stack:peek():start()
    end

    if self._stack:peek() then
        log:info('Current view: %s', self._stack:peek().__name)
    end
end

function ViewStack:swap(index, view)
    if not view then
        view = DummyView()
    end

    if index == self._stack:count() + 1 then
        -- pushing the next view
        self:push(view)
    elseif index == self._stack:count() then
        -- swapping the current view
        self._stack:peek():stop()
        self._stack:swap(index, view)
        self._stack:peek():start()
    else
        -- swapping a lower inactive view
        self._stack:swap(index, view)
    end

    log:info('Current view: %s', self._stack:peek().__name)
end

function ViewStack:clear()
    while self._stack:peek() do
        self:pop()
    end
end


class('ModeStack')
function ModeStack:_init()
    self._stack = Stack()
end

function ModeStack:trigger_mode(mode)
    beepio_bus.send_event('mode.' .. mode)
    for i, input_action in ipairs(input_actions) do
        input_action:set_active_mode(mode)
    end
end

function ModeStack:push(mode)
    self._stack:push(mode)
    self:trigger_mode(mode)
end

function ModeStack:pop()
    local mode = self._stack:pop()
    -- TODO: this is wrong
    if mode then
        self:trigger_mode(mode)
    end
end

function ModeStack:swap(index, mode)
    self._stack:swap(index, mode)
    if self._stack:count() == index then
        -- this is the new active mode, so send event
        self:trigger_mode(mode)
    end
end

function ModeStack:current()
    return self._stack:peek()
end

local ubus_conn
local system = beepio_model.system
local mode_stack = ModeStack()
local views = ViewStack()
local output_views
local mode



---- system event handlers and helpers

local function show_audio()
    local audio_state = system:get('audio_state')
    if audio_state == 'paused' then
        views:swap(1, output_views['audio_paused'])
    elseif audio_state == 'working' then
        views:swap(1, output_views['audio_working'])
    elseif audio_state == 'playing' then
        views:swap(1, output_views['audio_playing'])
    else
        log:warn('Unknown audio_state: %s', audio_state)
    end
end

local function on_system_mode_change()
    mode_stack:swap(1, system:get('mode'))
end

local function on_audio_state_change()
    if mode_stack:current() == 'audio' then
        show_audio()
    end
end

local temp_view_timer

local function temp_view_timer_done()
    -- if we're still in the audio mode, pop the temporary view, else
    -- it's already been popped
    if mode_stack:current() == 'audio' then
        views:pop()
    end
    temp_view_timer = nil
end

-- Use to push a timed temporary view. Currently only works in audio mode
-- because of the implementation of temp_view_timer_done
local function push_temp_view(view_name, millis)
    if output_views[view_name] then
        if temp_view_timer then
            views:pop()
            temp_view_timer:set(temp_view_timer_done, millis)
        else
            temp_view_timer = uloop.timer(temp_view_timer_done, millis)
        end
        views:push(output_views[view_name])
    end
end

local function on_volume_change()
    if mode_stack:current() == 'audio' then
        push_temp_view('volume', 2000)
    end
end


---- mode handlers

local function on_mode_starting()
    views:clear()
    views:push(output_views['starting'])
end

local function on_mode_audio()
    views:clear()
    show_audio()
end

local function on_mode_wifisetup()
    views:clear()
    views:push(output_views['wifisetup'])
end


-- actions
--
local function on_toggle_play_pause()
    local audio_state = system:get('audio_state')
    local playpause_method
    if audio_state == 'playing' or audio_state == 'working' then
        playpause_method = 'pause'
    else
        playpause_method = 'smart_resume'
    end
    ubus_call(ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = playpause_method,
        params = {}
    })
end

local function on_skip()
    ubus_call(ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = 'skip',
        params = {}
    })
end

local magic_timer

local function magic_timer_done()
    -- if we're still in the audio mode, pop the magic view, else
    -- it's already been popped
    if mode_stack:current() == 'audio' then
        views:pop()
    end
    magic_timer = nil
end

local function on_magic()
    if mode_stack:current() == 'audio' then
        push_temp_view('magic', 2000)
    end
    ubus_call(ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = 'play_magic',
        params = {}
    })
end

local function on_prev()
    ubus_call(ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = 'prev',
        params = {}
    })
end

local function on_volume_up()
    ubus_call(beepio_model.ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = 'adjust_volume',
        params = {
            players = {
                [beepio_model.local_id] = 42
            }}})
end

local function on_volume_down()
    ubus_call(beepio_model.ubus_conn, 'beep.head', 'call', {
        context = beepio_model.context,
        object = 'audio',
        method = 'adjust_volume',
        params = {
            players = {
                [beepio_model.local_id] = -42
            }}})
end

local function on_enter_wifisetup_mode()
    beepio_wifisetup.enable_setup_mode()
end

local function on_exit_wifisetup_mode()
    beepio_wifisetup.disable_setup_mode()
end

function run(conn, input_actions_, output_views_)
    ubus_conn = conn
    input_actions = input_actions_
    output_views = output_views_
    beepio_bus.add_handler('system.mode_change', 1, on_system_mode_change)
    beepio_bus.add_handler('system.audio_state_change', 1, on_audio_state_change)
    beepio_bus.add_handler('system.volume_change', 1, on_volume_change)

    beepio_bus.add_handler('action.toggle_play_pause', 1, on_toggle_play_pause)
    beepio_bus.add_handler('action.prev', 1, on_prev)
    beepio_bus.add_handler('action.skip', 1, on_skip)
    beepio_bus.add_handler('action.magic', 1, on_magic)
    beepio_bus.add_handler('action.volume_down', 1, on_volume_down)
    beepio_bus.add_handler('action.volume_up', 1, on_volume_up)
    beepio_bus.add_handler('action.enter_test_mode', 1, on_enter_test_mode)
    beepio_bus.add_handler('action.enter_wifisetup_mode', 1,
            on_enter_wifisetup_mode)
    beepio_bus.add_handler('action.exit_wifisetup_mode', 1,
            on_exit_wifisetup_mode)

    beepio_bus.add_handler('mode.starting', 1, on_mode_starting)
    beepio_bus.add_handler('mode.audio', 1, on_mode_audio)
    beepio_bus.add_handler('mode.wifisetup', 1, on_mode_wifisetup)

    -- first mode/view is always starting
    mode_stack:push('starting')
    views.push(output_views['starting'])
end
