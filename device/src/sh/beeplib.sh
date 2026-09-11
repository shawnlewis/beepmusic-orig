#!/bin/bash

# Virtual device control library

device_start() {
    _get_devname

    echo "Starting..."

    for component in $_BEEP_DEFAULT_COMPONENTS; do
        pid=$(_component_get_pid $BEEP_DEVNAME $component)
        if [ $pid == "0" ]; then
            _component_start $BEEP_DEVNAME $component $1
        fi
    done

    _show_pids $BEEP_DEVNAME
}

device_stop() {
    _get_devname

    local pids=$(_components_get_pids $BEEP_DEVNAME $_BEEP_COMPONENTS)

    if [ -n "$pids" ]; then
        echo "Stopping pids:" $pids
        _kill_pids $pids
    fi

    local file="/tmp/$BEEP_DEVNAME.ubus"

    if [ ! -e $file ]; then
        return 1
    fi

    if [ ! $NO_UBUS_STOP ]; then
        rm "/tmp/$BEEP_DEVNAME.ubus"
    fi
}

device_kill() {
    _get_devname

    local pids=$(_components_get_pids $BEEP_DEVNAME $_BEEP_COMPONENTS)
    echo "Killing pids:" $pids

    _force_kill_pids $pids
}

device_ps() {
    _get_devname

    ps -w -e -o pid,args | grep "$BEEP_DEVNAME.ubus" | grep -v grep
}

device_pids() {
    _get_devname

    _show_pids $BEEP_DEVNAME
}

device_ubus() {
    _get_devname

    _do_ubus $@
}

device_tail() {
    _get_devname

    tail -f ubusd.log -f beepmanager.log
}

device_log() {
    _get_devname

    if [ -z $EDITOR ]; then
        EDITOR=vi
    fi
    $EDITOR beepmanager.log
}

device_app_run() {
    BEEP_DEVNAME=$1
    shift

    _do_app_run $@
}

device_app_msg() {
    _get_devname

    _do_app_message $@
}

# on_devices [-q] <device_set> do: <command>
#     For each device directory in <device_set>, run <command>
#
#     Optionally, pass -q to suppress error messages (command output is
#     not suppressed).  The "do:" parameter is NOT OPTIONAL and separates
#     the device set from the command.
on_devices() {
    local device_set=""
    local device_count=0
    local in_dev_dir=false
    local quiet=false

    while [ "x$1" != "xdo:" -a "$#" -gt 1 ]; do

        if [ "x$1" == "x-q" ]; then
            quiet=true
        else
            device_set="$device_set $1"
            device_count=$((device_count + 1))
        fi

        shift
    done

    if [ -f "beep" ]; then
        if [ ! $quiet = true ]; then
            echo "Using device contained in current directory"
        fi
        in_dev_dir=true
    fi

    if [ ! $in_dev_dir = true -a $device_count -lt 1 ]; then
        if [ ! $quiet = true ]; then
            echo "Invalid arguments (no devices)"
        fi
        return 1
    fi

    if [ "$#" -lt 1 ]; then
        if [ ! $quiet = true ]; then
            echo "Invalid arguments (did you remember the '--'?)"
        fi
        return 2
    fi

    # Dump the -- separator
    shift

    if [ "$#" -lt 1 ]; then
        if [ ! $quiet = true ]; then
            echo "Invalid arguments (no command)"
        fi
        return 3
    fi

    local command=$1
    shift

    if [ $in_dev_dir = true ]; then
        # this is a device directory so just start this one
        $command $@
        return 0
    fi

    cd "$BEEPDEVS_DIR"

    # echo DEVICE_SET: $device_set
    # echo

    for dir in $device_set; do
        if [ ! -d $dir ]; then
            echo "ERROR: $dir is not a directory"
        else
            echo "DEVICE: $dir"
            cd $dir
            $command $@
            cd ..
            echo
        fi
    done
}

# all_devices
#    List all directories (hopefully device directories) in BEEPDEVS directory
all_devices() {
    cd "$BEEPDEVS_DIR"
    ls -d */ | sed "s/\/$//g"
    cd - >& /dev/null
}

# running_devices
#    List devices with active ubusd's
running_devices() {
    cd /tmp
    ls *.ubus 2>/dev/null | sed "s/\.ubus//g"
    cd - >& /dev/null
}

####################################################
##################### INTERNAL #####################
####################################################

_BEEP_CONFIG_FILE="$HOME/.beepsdk"

# arg1: key
_read_config_val() {
    key=$1
    val=$(grep $key "$_BEEP_CONFIG_FILE" | sed "s/^.*=//g")
    echo $val
}

if [ -z "$BEEPSDK_DIR" ]; then
    BEEPSDK_DIR=$(_read_config_val BEEPSDK_DIR)
    eval BEEPSDK_DIR="$BEEPSDK_DIR"
fi

if [ -z "$BEEPDEVS_DIR" ]; then
    BEEPDEVS_DIR="$(_read_config_val BEEPDEVS_DIR)"
    eval BEEPDEVS_DIR="$BEEPDEVS_DIR"
fi

if [ -z "$BEEPSDK_DIR" -o -z "$BEEPDEVS_DIR" ]; then
    echo "Invalid ~/.beepsdk: BEEPDEVS_DIR= and BEEPSDK_DIR= lines required."
    echo
    echo "Ex (~/.beepsdk):"
    echo "BEEPSDK_DIR=~/code/beep"
    echo "BEEPDEVS_DIR=~/code/beep/devices/src/testdevs"
    echo

    exit 1
fi

if [ -e $BEEPSDK_DIR/BEEP_RELEASE ]; then
    _BEEP_BIN_DIR="$BEEPSDK_DIR/beep"
    source "$BEEPSDK_DIR/env.sh" >& /dev/null
else
    _BEEP_BIN_DIR="$BEEPSDK_DIR/device/src/out/host"
    source "$BEEPSDK_DIR/device/src/env.sh" >& /dev/null
fi

_BEEP_LUA_DIR="$_BEEP_BIN_DIR/lua"

_BEEP_DEFAULT_COMPONENTS="ubusd beepio beepmanager"
_BEEP_COMPONENTS="ubusd urelay uhttpd beepmanager distributor playnet beephead beepcomm beepcloud beepdiscovery beepio beephealth beepjs beepalsa beepdummy app_spotify app_generic gmrender nest"

_word_count() {
    i=0
    for token in $1; do
        i=$((i+1))
    done
    return $i
}

_wait_pids() {
    local cycles=$1
    shift

    done_count=0
    for pid in $@; do
        while [ 1 ]; do
            kill -0 "$pid" >& /dev/null
            if [ $? ]  ; then
                done_count=$((done_count+1))
                break
            fi
            sleep .1
        done
    done

    if [ $done_count -eq $# ]; then
        return 1
    else
        return 0
    fi
}


# tries to SIGTERM pids before SIGKILL'ing them.
_kill_pids() {
    kill $@ >& /dev/null
    _wait_pids 10 $@   # wait 1 second

    if [ $? -eq 0 ]; then
        _force_kill_pids $@
    fi
}

_force_kill_pids() {
    kill -9 $@ >& /dev/null
    _wait_pids 10 $@
    if [ $? -eq 0 ]; then
        echo "Couldn\'t kill pids $@ (some may be dead, but not all)"
        exit 1
    fi
}

_component_check() {
    local component=$1
    if [[ "$_BEEP_COMPONENTS" != *$component* ]]; then
        echo "Invalid component for component_get_pid: $component"
        exit 1
    fi
}

_component_get_pid() {
    local devname=$1
    local component=$2
    _component_check $component

    if [ $component == "beepalsa" -o $component == "beepdummy" ]; then
        pid="$(pgrep -f $component -P $(_component_get_pid $devname playnet))"
    else
        pid="$(pgrep -f $component.*\/$devname\.ubus)"
    fi
    if [ $? -ne 0 ]; then
        pid=0
    else
        # make sure we only found one process
        _word_count $pid
        pid_count=$?
        if [ $pid_count -gt 1 ]; then
            echo "Got more than one ubus pid?"
            exit 1
        fi
    fi

    echo $pid
}

_components_get_pids() {
    local devname=$1
    shift

    # hack: we know boom is the first component passed in, so we can shift
    # to not stop it, if requested.
    if [ $NO_UBUS_STOP ]; then
        shift
    fi

    pids=""
    for component in $@; do
        pid=$(_component_get_pid $devname $component)
        if [[ $pid != "0" ]]; then
            pids="$pid $pids"
        fi
    done

    echo $pids
}

_component_start() {
    local devname=$1
    local component=$2

    _component_check $component

    local sock_path="/tmp/$devname.ubus"

    if [ $component = "ubusd" ]; then
        cmd="ubusd -s $sock_path"
    elif [ $component = "beepio" ]; then
        cmd="/usr/bin/lua $_BEEP_LUA_DIR/beepio.lua --ubus=$sock_path --uciconfig=$PWD"
    elif [ $component = "beepmanager" ]; then
        playnet_port=$(uci -q -c . get beep_devel.boom.playnet_port)
        urelay_port=$(uci -q -c . get beep_devel.boom.urelay_port)
        uhttpd_port=$(uci -q -c . get beep_devel.boom.uhttpd_port)
        spotify_port=$(uci -q -c . get beep_devel.boom.spotify_port)
        ssdp_port=$(uci -q -c . get beep_devel.boom.ssdp_port)
        dial_port=$(uci -q -c . get beep_devel.boom.dial_port)
        msg_port=$(uci -q -c . get beep_devel.boom.msg_port)
        cmd="/usr/bin/lua $_BEEP_LUA_DIR/beepmanager.lua \
            --ubus=$sock_path
            --uciconfig=$PWD \
            --no_beephead_simple \
            --vm"
        if [ -n "$playnet_port" ]; then
            cmd="$cmd --playnet_port=$playnet_port"
        fi
        if [ -n "$urelay_port" ]; then
            cmd="$cmd --urelay_port=$urelay_port"
        fi
        if [ -n "$uhttpd_port" ]; then
            cmd="$cmd --uhttpd_port=$uhttpd_port"
        fi
        if [ -n "$spotify_port" ]; then
            cmd="$cmd --spotify_port=$spotify_port"
        fi
        if [ -n "$ssdp_port" ]; then
            cmd="$cmd --ssdp_port=$ssdp_port"
        fi

        if [ -n "$dial_port" ]; then
            cmd="$cmd --dial_port=$dial_port"
        fi

        if [ -n "$msg_port" ]; then
            cmd="$cmd --msg_port=$msg_port"
        fi
    fi

    cur_dir="$(readlink -f $PWD)"
    cd $_BEEP_BIN_DIR > /dev/null
    if [ -n "$NOLOG" ]; then
        $cmd &
    else
        $cmd 2>&1 | cat > "$cur_dir/$component.log" &
    fi
    cd - > /dev/null
}

_show_pids() {
    local devname=$1

    for component in $_BEEP_COMPONENTS; do
        echo $component $(_component_get_pid $devname $component)
    done | column -t
}

_do_ubus() {
    local pid="$(_component_get_pid $BEEP_DEVNAME ubusd)"

    # Check if ubusd is running first
    if [ $pid -eq 0 ]; then
        echo "ubusd not running for $BEEP_DEVNAME"
        exit 1
    fi

    if [ $1 == "call" -a $# -gt 3 ]; then # call?
        local function=$1
        local obj=$2
        local method=$3
        shift 3

        local params=""
        for fragment in "$@"; do
            params="$params $fragment"
        done

        ubus -s "/tmp/$BEEP_DEVNAME.ubus" $function $obj $method "$params"
    else
        ubus -s "/tmp/$BEEP_DEVNAME.ubus" "$@"
    fi

}

_do_app_run() {
    local app_name="$1"
    shift

    export BEEPJS_PATH="$_BEEP_BIN_DIR/js"
    "$_BEEP_BIN_DIR/beepjs" "$app_name" --ubus=/tmp/$BEEP_DEVNAME.ubus --uciconfig="$dev_dir"

    local retval=$?
    return $retval
}

_do_app_message() {
    local app_obj="$1"
    shift

    local inner_msg="$(echo $@ | sed s/\\\"/\\\\\\\"/g)"

    local msg=$(echo \
            '{"sender_id": "1",'\
            '"namespace": "urn:x-cast:com.google.cast.media",'\
            "\"message\": \"$inner_msg\""\
            '}')
    echo $msg

    _do_ubus call $app_obj msg_socket_message_received "$msg"
}

# Sets the global BEEP_DEVNAME based on current directory or empty string
# if not in a device directory
_get_devname() {
    if [ -e "beep_data" ]; then
        BEEP_DEVNAME="$(basename $PWD)"
    else
        echo "Invalid device directory"
        exit -1
    fi
}

