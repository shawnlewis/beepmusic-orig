-- Provides a singleton for launching beep agents.
-- TODO: figure out where to kill previous processes.
-- TODO: kill timed out processes

require 'util'
local log = require 'log'

local AgentManager = {
    agents = {},
    _init = false,
}

-- Initialize the agent manager
-- Args:
--     conn: ubus connection
function AgentManager.init(self, conn)
    -- Make this idempotent
    if self._init then
        return
    end

    -- register for 'ubus.object.add' and 'ubus.object.remove'
    self.conn = conn
    self.conn:listen(
        'ubus.object.add',
        function(event, msg)
            self:_on_ubus_object_add(msg.path)
        end)
    self.conn:listen(
        'ubus.object.remove',
        function(event, msg)
            self:_on_ubus_object_remove(msg.path)
        end)
    self._init = true
end

-- Start a beep agent.
--
-- This executes the beep agent given by exec_path as a subprocess. The
-- agent is expected to connect to ubus and provide a ubus object.
--
-- AgentManager can also watch processes that don't provide a ubus object.
-- Pass ubus_object_path = nil for this behavior.
--
-- Args:
--     exec_path: path to agent executable; this can be nil if you want to
--          start the object at a later time through some other means
--     ubus_object: ubus object name added by the agent
--     timeout: milliseconds to wait before canceling the process; ignored if
--          exec_path is nil
--     on_added: function to be called when ubus object is added
--     on_start_timeout: function to be called if object is not added within
--          timeout period; ignored if exec_path is nil
--     on_removed: function to be called when ubus object is removed
function AgentManager.start_agent(
        self, exec_path, env, ubus_object, timeout,
        on_added, on_start_timeout, on_removed)
    local proc = nil

    local agent = {
        managed = (exec_path ~= nil),
        started=false,
        on_added=on_added,
        on_removed=on_removed,
        on_removed_called=false,
    }

    if exec_path then
        local fields = string_split(exec_path)
        local command = fields[1]
        table.remove(fields, 1)

        log:info('Running: ' .. exec_path)
        if env then
            log:info('With env: %s', env)
        end
        agent.proc = uloop.process(command, fields, env, nil, function()
            if not agent.on_removed_called then
                agent.on_removed()
                agent.on_removed_called = true
            end
        end)
    end

    if ubus_object and agent.managed then
        agent.timer = uloop.timer(
            function()
                if not agent.on_removed_called then
                    on_start_timeout()
                    self.agents[ubus_object] = nil
                end
            end, timeout)
    end

    if ubus_object then
        self.agents[ubus_object] = agent
    end

    return agent.proc
end

-- Private
function AgentManager._on_ubus_object_add(self, ubus_path)
    local agent = self.agents[ubus_path]
    if not agent then
        return
    end
    if agent.started and agent.managed then
        log:error('agent start event received when already started')
        return
    end
    agent.started = true

    agent.on_added(agent.proc:get_pid())

    if agent.managed then
        agent.on_added = nil
        agent.timer:cancel()
    end

end

-- Private
function AgentManager._on_ubus_object_remove(self, ubus_path)
    local agent = self.agents[ubus_path]
    if not agent then
        return
    end

    if not agent.started and agent.managed then
        log:error('agent remove event received when agent not stated')
        return
    end

    if not agent.on_removed_called then
        agent.on_removed()
        agent.on_removed_called = true
    end

    if agent.managed then
        self.agents[ubus_path] = nil
    end
end

return AgentManager

