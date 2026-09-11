import os

import beep_scons as beep


def exists(env):
    return True

def generate(env, **kwargs):
    # Copy the build tools from the shell environment to the scons build env.
    shell_key_list = [
        'AR',
        'AS',
        'CC',
        'CXX',
        'GCC',
        'LD',
        'NM',
        'OBJCOPY',
        'OBJDUMP',
        'RANLIB',
        'SIZE',
        'STRIP'
    ]
    beep.copy_keys_to_env(env, shell_key_list)

    shell_clvar_key_list = [
        'CFLAGS',
        'CXXFLAGS',
        'LDFLAGS'
    ]
    beep.copy_keys_to_env_clvar(env, shell_clvar_key_list)

    # Copy some variables from the shell environment to the environment
    # created by scons.
    env.Append(ENV={'STAGING_DIR': os.environ['STAGING_DIR']})
    env.Append(ENV={'PATH': ':'.join([env['ENV']['PATH'],
            os.environ['PATH']])})

    # Setup the sysroot as provided by buildroot.
    env['SYSROOT'] = os.environ['STAGING_DIR']
