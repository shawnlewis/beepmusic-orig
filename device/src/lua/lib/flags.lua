-- Simple flags lib, we should use this for all command line args.
--
-- In any module do:
--
-- local flags = require 'flags'
-- flags.add(flagname, take_arg)  # where flag_name is the name of the flag
--                                # and takes_arg is a boolean that specifies
--                                # whether the flag requires an argument
--
-- In your main do: flags.init(arg)
--
-- All parsed flags and their values will be available in the flags.flags
-- table.
--
--
-- Missing features:
--   - Flag argument type checking/parsing
--   - takes_arg doesn't do anything, all flags currently must take an argument

local M = {}

defs = {}

M.flags = {}

function M.add(name, takes_arg, default)
    defs[name] = takes_arg
    M.flags[name] = default
end

function usage()
    print('Usage:')
    for flag_name, takes_arg in pairs(defs) do
        if takes_arg then
            print('  --' .. flag_name .. '=' .. '<arg>')
        else
            print('  --' .. flag_name)
        end
    end
end

function M.init(args)
    local left_args = {}
    for i, v in ipairs(args) do
        if string.sub(v, 1, 2) == '--' then
            if string.sub(v, 3) == 'help' then
                usage()
                os.exit()
            end

            local equal_pos = string.find(v, '=', 1, true)
            if equal_pos then
                local fname = string.sub(v, 3, equal_pos - 1)
                local fval = string.sub(v, equal_pos + 1)
                if not defs[fname] then
                    usage()
                    os.exit(1)
                else
                    M.flags[fname] = fval
                end
            elseif defs[string.sub(v,3)] == false then
                local fname = string.sub(v,3)
                M.flags[fname] = true
            else
                usage()
                os.exit(1)
            end
        else
            table.insert(left_args, v)
        end
    end
    return left_args
end

return M
