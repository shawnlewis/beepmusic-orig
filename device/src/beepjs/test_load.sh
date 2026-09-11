msg=$(echo \
    '{"sender_id": "1",'\
    '"namespace": "urn:x-cast:com.google.cast.media",'\
    '"message": "{'\
        '\"type\": \"LOAD\",'\
        '\"requestId\": \"5\",'\
        '\"media\": {'\
            '\"contentId\": \"http://www.stephaniequinn.com/Music/Commercial%20DEMO%20-%2005.mp3\",'\
            '\"streamType\": \"bla\",'\
            '\"contentType\": \"bla\"'\
    '}}"}'
)
boom ubus call beep.app.sample msg_socket_message_received "$msg"

sleep 5

msg=$(echo \
    '{"sender_id": "1",'\
    '"namespace": "urn:x-cast:com.google.cast.media",'\
    '"message": "{'\
        '\"mediaSessionId\": \"14\",'\
        '\"type\": \"VOLUME\",'\
        '\"requestId\": \"6\",'\
        '\"volume\": {'\
            '\"level\": 0.3'\
    '}}"}'
)
boom ubus call beep.app.sample msg_socket_message_received "$msg"

msg=$(echo \
    '{"sender_id": "1",'\
    '"namespace": "urn:x-cast:com.google.cast.media",'\
    '"message": "{'\
        '\"mediaSessionId\": \"14\",'\
        '\"type\": \"VOLUME\",'\
        '\"requestId\": \"6\",'\
        '\"volume\": {'\
            '\"muted\": true'\
    '}}"}'
)
boom ubus call beep.app.sample msg_socket_message_received "$msg"

msg=$(echo \
    '{"sender_id": "1",'\
    '"namespace": "urn:x-cast:com.google.cast.media",'\
    '"message": "{'\
        '\"mediaSessionId\": \"14\",'\
        '\"type\": \"PAUSE\",'\
        '\"requestId\": \"6\"'\
    '}"}'
)
boom ubus call beep.app.sample msg_socket_message_received "$msg"

sleep 2

msg=$(echo \
    '{"sender_id": "1",'\
    '"namespace": "urn:x-cast:com.google.cast.media",'\
    '"message": "{'\
        '\"mediaSessionId\": \"14\",'\
        '\"type\": \"PLAY\",'\
        '\"requestId\": \"6\"'\
    '}"}'
)
boom ubus call beep.app.sample msg_socket_message_received "$msg"
