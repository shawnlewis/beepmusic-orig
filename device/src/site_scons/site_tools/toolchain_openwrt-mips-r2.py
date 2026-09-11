import os


def exists(env):
    return True

def generate(env, **kwargs):
    staging_root = os.path.abspath('../openwrt/staging_dir')
    target_staging_dir = os.path.join(staging_root,
            'target-mips_r2_uClibc-0.9.33.2')
    toolchain_prefix = os.path.join(staging_root,
            'toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin/mips-openwrt-linux-')

    env['AR'] = toolchain_prefix + 'ar'
    env['AS'] = toolchain_prefix + 'as'
    env['CC'] = toolchain_prefix + 'gcc'
    env['CXX'] = toolchain_prefix + 'g++'
    env['GCC'] = toolchain_prefix + 'gcc'
    env['LD'] = toolchain_prefix + 'ld'
    env['NM'] = toolchain_prefix + 'nm'
    env['OBJCOPY'] = toolchain_prefix + 'objcopy'
    env['OBJDUMP'] = toolchain_prefix + 'objdump'
    env['RANLIB'] = toolchain_prefix + 'ranlib'
    env['SIZE'] = toolchain_prefix + 'size'
    env['STRIP'] = toolchain_prefix + 'strip'

    env.Append(CCFLAGS=[
        '-Os',
        '-pipe',
        '-mips32r2',
        '-mtune=34kc',
        '-fno-caller-saves',
        '-mno-branch-likely',
        '-fhonour-copts',
        '-msoft-float'
    ])

    env['SYSROOT'] = os.path.join(staging_root,
            'target-mips_r2_uClibc-0.9.33.2')

    env.Append(ENV={'STAGING_DIR': target_staging_dir})
    env.Append(LIBPATH=[os.path.join(target_staging_dir, 'usr/lib')])
