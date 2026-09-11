#!/usr/bin/lua
require 'beep_ubus'
require 'ubus'

local flags = require 'flags'
local log = require 'log'

local beepio_config = require 'beepio_config'

flags.init(arg)
log:init('beepio')

beepio_config.load()
