local crypto = require 'crypto'

local inspect = require 'inspect'

function make_class()
    local this_class = {}
    this_class.__index = this_class

    setmetatable(this_class, {
      --__index = this_class,
      __call = function (cls, ...)
        local self = setmetatable({}, cls)
        if self._init then
            self:_init(...)
        end
        return self
      end,
    })

    return this_class
end

function class(name, base)
    local new_cls = {}
    new_cls.__index = new_cls

    -- class name
    new_cls.__name = name

    -- use to access base table.
    new_cls.__base = base,

    setmetatable(new_cls, {
      __index = base,
      __call = function (cls, ...)
        local self = setmetatable({}, cls)
        if self._init then
            self:_init(...)
        end
        return self
      end,
    })

    -- put the class in the caller's environment. This works if you use the
    -- module() function to define modules that contain classes.
    local env = getfenv(2)
    env[name] = new_cls
end

function bind (t, k)
    return function(...) return t[k](t, ...) end
end

function new_uuid()
    return io.open('/proc/sys/kernel/random/uuid'):read()
end

function generate_uuid_from_ids(ids)
    crypto.init()
    local d = crypto.md5(ids)
    crypto.free()

    local uuid = table.concat(
            {
                d:sub(0,8),
                d:sub(9,12),
                d:sub(13,16),
                d:sub(17,20),
                d:sub(21)
            },'-')
    return uuid
end

function table_length(t)
    local count = 0
    for _ in pairs(t) do
        count = count + 1
    end
    return count
end

function array_contains(a, v)
    for i, obj in ipairs(a) do
        if obj == v then
            return true
        end
    end
    return false
end

function array_index(a, v)
    for i, obj in ipairs(a) do
        if obj == v then
            return i
        end
    end
    return -1
end


function string_starts(s, prefix)
    if not s then
        return false
    end
    return string.sub(s, 1, string.len(prefix)) == prefix
end

function string_ends(s, suffix)
    if not s then
        return false
    end
    return string.sub(s,-string.len(suffix)) == suffix
end

function string_strip(s)
    return s:gsub("^%s*(.-)%s*$", "%1")
end

function bool_to_num(val)
    if val then
        return 1
    else
        return 0
    end
end

-- returns a table whose keys are the union of keys in the passed in
-- tables and whose values are the corresponding values from the source
-- tables. Last table to have a key wins.
function concat_tables(...)
    local t = {}
    for i, source in ipairs(arg) do
        for k, v in pairs(source) do
            t[k] = v
        end
    end
    return t
end

-- true if pred(v) == true for all v in list
function all(list, pred)
    local result = true
    for i, v in ipairs(list) do
        if not pred(v) then
            result = false
        end
    end
    return result
end

-- true if pred(v) == true for all k, v in table
function all_vals(list, pred)
    local result = true
    for k, v in pairs(list) do
        if not pred(v) then
            result = false
        end
    end
    return result
end

-- true if pred(v) == true for any k, v in table
function any_vals(list, pred)
    local result = false
    for k, v in pairs(list) do
        if pred(v) then
            result = true
        end
    end
    return result
end

function shallowcopy(orig)
    local orig_type = type(orig)
    local copy
    if orig_type == 'table' then
        copy = {}
        for orig_key, orig_value in pairs(orig) do
            copy[orig_key] = orig_value
        end
    else -- number, string, boolean, etc
        copy = orig
    end
    return copy
end

function deepcopy(orig)
    local orig_type = type(orig)
    local copy
    if orig_type == 'table' then
        copy = {}
        for orig_key, orig_value in next, orig, nil do
            copy[deepcopy(orig_key)] = deepcopy(orig_value)
        end
        setmetatable(copy, deepcopy(getmetatable(orig)))
    else -- number, string, boolean, etc
        copy = orig
    end
    return copy
end

function sum(t)
    local sum = 0
    for k,v in pairs(t) do
        sum = sum + v
    end

    return sum
end

function iter_to_array(iter)
    local t = {}
    for item in iter do
        table.insert(t, item)
    end
    return t
end

-- splits a string by whitespace
function string_split(s)
    return iter_to_array(string.gmatch(s, '%S+'))
end

-- splits a string by a pattern.
-- TODO: Merge this with the above function. But we need a unit test first.
-- from lua-users.org/wiki/SplitJoin
function string_split_by(str, pat)
    local t = {}  -- NOTE: use {n = 0} in Lua-5.0
    local fpat = "(.-)" .. pat
    local last_end = 1
    local s, e, cap = str:find(fpat, 1)
    while s do
        if s ~= 1 or cap ~= "" then
            table.insert(t, cap)
        end
        last_end = e + 1
        s, e, cap = str:find(fpat, last_end)
    end
    if last_end <= #str then
        cap = str:sub(last_end)
        table.insert(t, cap)
    end
    return t
end

function deep_equals(t1, t2)
    local ty1 = type(t1)
    local ty2 = type(t2)
    if ty1 ~= ty2 then
        return false
    end
    -- non-table types can be directly compared
    if ty1 ~= 'table' and ty2 ~= 'table' then
        return t1 == t2
    end
    for k1, v1 in pairs(t1) do
        local v2 = t2[k1]
        if v2 == nil or not deep_equals(v1,v2) then
            return false
        end
    end
    for k2, v2 in pairs(t2) do
        local v1 = t1[k2]
        if v1 == nil or not deep_equals(v1,v2) then
            return false
        end
    end
    return true
end

function keys(t)
    local keys = {}
    for k, v in pairs(t) do
        table.insert(keys, k)
    end
    return keys
end

-- returns true if t1 keys are subset of t2 keys
function is_subset_of(t1, t2)
    for key, _ in pairs(t1) do
        if not t2[key] then
            return false
        end
    end
    return true
end

-- returns t1 - t2
function set_subtract(t1, t2)
    local result = shallowcopy(t1)
    for k, _ in pairs(t2) do
        result[k] = nil
    end
end

function sets_equal(t1, t2)
    return is_subset_of(t1, t2) and is_subset_of(t2, t1)
end

-- From: http://stackoverflow.com/questions/2834579/print-all-local-variables-accessible-to-the-current-scope-in-lua
function locals(level)
    if not level then
        level = 2
    end
    local variables = {}
    local idx = 1
    while true do
        local ln, lv = debug.getlocal(level, idx)
        if ln ~= nil then
            variables[ln] = lv
        else
            break
        end
        idx = 1 + idx
    end
    return variables
end

-- From: http://stackoverflow.com/questions/2834579/print-all-local-variables-accessible-to-the-current-scope-in-lua
function upvalues()
    local variables = {}
    local idx = 1
    local func = debug.getinfo(2, "f").func
    while true do
        local ln, lv = debug.getupvalue(func, idx)
        if ln ~= nil then
            variables[ln] = lv
        else
            break
        end
        idx = 1 + idx
    end
    return variables
end

function get_command_output(cmd)
    local f = assert(io.popen(cmd, 'r'))
    local s = f:read('*a')
    f:close()
    return s
end
