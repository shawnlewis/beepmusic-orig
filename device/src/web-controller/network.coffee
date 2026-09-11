enter_error_state = ->
    error_state = true
    console.log('ERROR STATE')

$.ajaxSetup({timeout: 30000})

##### json rpc

json_rpc_id = 0

make_json_rpc_response_handler = (expected_json_rpc_id, on_response, on_error) ->
    (response) ->
        if response.id != expected_json_rpc_id
            console.log("Got json_rpc_id #{response.id} but expected "
                    "#{json_rpc_id}")
            enter_error_state()
        else if response.error
            on_error(response.error.code, response.error.message)
        else
            on_response(response.result)

window.json_rpc = (method, params, on_response, on_timeout) ->
    json_rpc_id += 1
    handler = make_json_rpc_response_handler(
        json_rpc_id,
        on_response,
        (error_code, error_message) ->  # error handler
            if error_code == -32003 and on_timeout
                on_timeout()
            else
                console.log("Got json-rpc error. code: #{error_code} "
                            "message: #{error_message}")
                enter_error_state())

    $.post('/synapse',
           JSON.stringify(
               jsonrpc: '2.0',
               method: method,
               id: json_rpc_id,
               params: params))
        .success(handler)
        # TODO: Need to check for timeout
        .error((jqXHR, error_type, exc) ->
            if error_type == "timeout" and on_timeout
                on_timeout()
            else
                console.log("json-rpc error")
                enter_error_state()
        )

##### ubus

window.ubus_call = (path, method, params, on_response, on_timeout) ->
    on_ubus_response = (response) ->
        if (response[0] == 7) # ubus timeout
            on_timeout()
        else
            on_response(response[1])
    json_rpc('call', [path, method, params], on_ubus_response, on_timeout)


##### beep

make_beep_response_handler = (on_success) ->
    (response) ->
        if (!response)
            console.log("No response")
            enter_error_state()
        else if (response.success)
            if (on_success)
                on_success(response.result)
        else
            console.log("Got error. code: #{response.error_code}"
                        " message: #{response.error_message}")
            enter_error_state()

window.beep_request = (group, object, method, params, success) ->
    ubus_call('beep.head', 'call', {
        context: group,
        object: object,
        method: method,
        params: params or {}},
        make_beep_response_handler(success))


count = 0
window.StateSync = class StateSync

    constructor: (on_state_change) ->
        @on_state_change = on_state_change
        @seq = 0
        @do_long_poll()

    do_long_poll: =>
        console.log('Doing long poll')
        on_success = make_beep_response_handler (result) =>
            @seq = result.seq
            console.log('result seq ' + result.seq)
            for key, events of result.data
                @on_state_change(key, events)
            @do_long_poll()

        ubus_call('beep.head', 'events', {seq: @seq},
            on_success,
            @do_long_poll)  # on_timeout
