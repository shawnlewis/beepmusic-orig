import commands
import glob
import os

import SCons
from targetdb import targetdb


DEFAULT_FG = 4  # blue

LOG_LEVEL_ERROR = 0
LOG_LEVEL_WARN = 1
LOG_LEVEL_INFO = 2
LOG_LEVEL_DEBUG = 3

BINVER_CACHE_SEP = '::::'

def colorstr(s, **kwargs):
    colors = {
        'black':    0,
        'red':      1,
        'green':    2,
        'yellow':   3,
        'blue':     4,
        'magenta':  5,
        'cyan':     6,
        'white':    7,
        'default-fg': DEFAULT_FG
    }
    codes = []
    if 'fg' in kwargs.keys():
        codes.append(30 + colors.get(kwargs['fg'], 7))
    if 'bg' in kwargs.keys():
        codes.append(40 + colors.get(kwargs['bg'], 0))
    if kwargs.get('bold', False):
        codes.append(1)

    if len(codes) == 0:
        return s

    return '\033[' + ';'.join([str(x) for x in codes]) + 'm' + s + '\033[0m'

def pretty_com_str(com, args):
    ret = ['   ']  # Only 3 spaces since this is joined.
    if com is not None:
        ret.append(colorstr(com, bold=True, fg='default-fg'))
    if args is not None:
        ret.append(args)
    return ' '.join(ret)

def log(level, args):
    level_str = {
        LOG_LEVEL_ERROR: 'ERROR',
        LOG_LEVEL_WARN: 'WARN',
        LOG_LEVEL_INFO: 'INFO',
        LOG_LEVEL_DEBUG: 'DEBUG'
    }
    level_color = {
        LOG_LEVEL_ERROR: 'red',
        LOG_LEVEL_WARN: 'green',
        LOG_LEVEL_INFO: 'yellow',
        LOG_LEVEL_DEBUG: 'blue'
    }
    if level not in level_str.keys():
        level = LOG_LEVEL_ERROR

    cstr = colorstr(level_str[level], bold=True, fg=level_color[level])
    msg = ' '.join([str(x) for x in args])
    msg = msg.split('\n')
    msg = '\n'.join(['{}: {}'.format(cstr, x) for x in msg])
    print(msg)

def quick_cmd(cmd):
    s, o = commands.getstatusoutput(cmd)
    if (s != 0):
        return None
    return o

def gcc_flag_test(gcc, flag):
    cmd = '{} {} -E - < /dev/null'.format(gcc, flag)
    s, o = commands.getstatusoutput(cmd)
    return (s == 0)

def list_targets():
    print 'Target list:'
    print str(targetdb)

def list_toolchains():
    tools_dir = os.path.join(SCons.Script.Dir('#').abspath,
        'site_scons/site_tools')
    tools = glob.glob(os.path.join(tools_dir, 'toolchain_*.py'))
    tools = [x[len(os.path.join(tools_dir, 'toolchain_')):-3] for x in tools]
    tools.sort()

    print 'Toolchain list:'
    print '\n'.join(tools)

def copy_keys_to_env(env, key_list):
    shell_env = os.environ
    for key in [x for x in key_list if shell_env.has_key(x)]:
        env[key] = shell_env[key]

def copy_keys_to_env_clvar(env, key_list):
    shell_env = os.environ
    for key in [x for x in key_list if shell_env.has_key(x)]:
        env[key] = SCons.Util.CLVar(shell_env[key])

# Generally this is going to be used by OpenWrt which uses a different python
# than the rest of the system.  This is just a simple way to save the defines
# to make sure there are not any incompatibilities.
def save_binver_defines(d, path):
    f = open(path, 'w')
    for k, v in d.items():
        f.write('{}{}{}{}{}\n'.format(k, BINVER_CACHE_SEP, type(v).__name__,
            BINVER_CACHE_SEP, v))
    f.close()

def load_binver_defines(path):
    try:
        f = open(path, 'r')
        data = f.read()
        f.close()
    except:
        LogInfo('Could not read from file {}'.format(path))
        return {}

    data = [x.split(BINVER_CACHE_SEP) for x in data.split('\n') if x != '']
    d = {}
    for k, t, v in data:
        if t == 'int':
            v = int(v)
        elif t != 'str':
            print('Dropping unsupported type {} for {}:{}'.format(t, k, v))
        d[k] = v
    return d

def get_binver_defines():
    rev = quick_cmd('git rev-parse HEAD')
    if rev == None:
        rev = '0000000000000000000000000000000000000000'
    quick_cmd('git update-index -q --ignore-submodules --refresh')
    # If unstaged or uncommited are None (i.e. $? != 0) then there are
    # changes to the repository.
    unstaged = quick_cmd('git diff-files --quiet --ignore-submodules --')
    uncommited = quick_cmd('git diff-index --cached --quiet HEAD --ignore-submodules --')
    clean = None not in [unstaged, uncommited]
    openwrt = 'OPENWRT_BUILD' in os.environ.keys()
    user = os.environ.get('USER', 'unknown')

    # Limit defines to hardcoded size in BeepVersion (see beep_ver.h)
    rev = rev[:40]
    user = user[:32]

    defs = {
        'VER_SET_BUILDER': '\'"{}"\''.format(user),
        'VER_SET_GIT_REV': '\'"{}"\''.format(rev)
    }
    if openwrt == True:
        defs['VER_SET_OPENWRT'] = 1
    if clean == True:
        defs['VER_SET_GIT_CLEAN'] = 1
    else:
        defs['VER_SET_GIT_DIRTY'] = 1

    return defs
