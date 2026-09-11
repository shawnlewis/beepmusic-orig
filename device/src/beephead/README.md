Overview
--------

beephead runs on every device and handles all communication between controllers
and the rest of the beep system.  It is implemented as a ubus object, and relies
on a jsonrpc interface exposed by uhttpd to enable controllers (such as web
controllers and android/ios controllers) to communicate with it.

The standard uhttpd setup is as follows:

    - port 8070
    - ubus binding path set to '/synapse'
    - authentication disabled

    (NOTE: this requires read/write access to the ubus socket, default = /var/run/ubus.sock)

This allows controllers to make requests against beephead like so:

    POST to http://<device_host>:8070/synapse with body:

    {
        "jsonrpc":"2.0",
        "id":<id>                   <-- NOTE: This is optional
        "method":"call",
        "params":
        [
            "<object_path>",
            "<object_method>",
            {
                "param1":value1,
                ...
                "paramN":valueN
            }
        ]
    }

Responses look like this:

    {
        "jsonrpc":"2.0",
        "id":<id>,                  <-- NOTE: This will be null if you did not provide one
        "result":
        [
            <result_code>           <-- This is 0 on success
            { msg. from object }
        ]
    }

Currently, synapse exposes all ubus objects, but you should only ever talk to beephead.
beephead exposes a number of methods used to control the beep system, outlined below:

    
