SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
if [ -d "$SCRIPT_DIR/beep" ]; then
    BEEP_DIR="$SCRIPT_DIR/beep"
else
    BEEP_DIR="$SCRIPT_DIR/out/host"
fi

echo BEEP_DIR: $BEEP_DIR

export LUA_PATH="./?.lua;$BEEP_DIR/lua/?.lua;$BEEP_DIR/lua/lib/?.lua;$BEEP_DIR/lua/test/?.lua;/usr/local/share/lua/5.1/?.lua;/usr/local/share/lua/5.1/?/init.lua"
echo LUA_PATH: $LUA_PATH

export LUA_CPATH="$BEEP_DIR/lualib/?.so;./?.so;/usr/local/lib/lua/5.1/?.so;/usr/lib/i386-linux-gnu/lua/5.1/?.so;/usr/lib/lua/5.1/?.so;/usr/local/lib/lua/5.1/loadall.so"
echo LUA_CPATH: $LUA_CPATH

export LD_LIBRARY_PATH="$BEEP_DIR"
export PATH=$PATH:$SCRIPT_DIR

