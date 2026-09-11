#!/usr/bin/env lua

local flags = require 'flags'
local log = require 'log'

require 'beepio_clock'
require 'beepio_config'
require 'beepio_controller'
require 'beepio_comm_i2c'
require 'beepio_comm_gpio'
require 'beepio_comm_ubus'
require 'beepio_model'

flags.init(arg)
log:init('beepio')

uloop.init()

local conn = beep_ubus_connect('beepio')

-- initialize modules
beepio_clock.init()
beepio_comm_ubus.set_ubus_conn(conn)
beepio_comm_ubus.init()
beepio_comm_gpio.init()
beepio_model.init(conn)

-- load the config file
inputs, input_actions, outputs, output_views = beepio_config.load()

beepio_comm_i2c.start() -- NOOP if i2c inputs/outputs not found
beepio_comm_gpio.start() -- NOOP if gpio/i2c inputs/outputs found.

beepio_controller.run(conn, input_actions, output_views)

uloop.run()
