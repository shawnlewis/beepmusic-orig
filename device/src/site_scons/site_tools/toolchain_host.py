import optparse

import SCons
import beep_scons as beep


def exists(env):
    return True

def generate(env, **kwargs):
    asan_supported = False
    tsan_supported = False
    need_no_omit_fp = True

    env.SetDefault(GCC='gcc')
    env.SetDefault(LD='ld')
    env.SetDefault(NM='nm')
    env.SetDefault(OBJCOPY='objcopy')
    env.SetDefault(OBJDUMP='objdump')
    env.SetDefault(SIZE='size')
    env.SetDefault(STRIP='strip')

    cc = env.get('CC', None)
    if cc:
        asan_supported = beep.gcc_flag_test(cc, '-fsanitize=address')
        tsan_supported = beep.gcc_flag_test(cc, '-fsanitize=thread')

    if asan_supported:
        try:
            SCons.Script.AddOption(
                    '--asan',
                    help='Use libasan (no, yes, static) host only',
                    dest='asan',
                    type='string',
                    nargs=1,
                    action='store',
                    default='no')
        except (optparse.OptionConflictError):
            # Ignore errors if this is added multiple times.
            pass

        opt_asan = SCons.Script.GetOption('asan').lower()
        if opt_asan in ['yes', 'y', 'static']:
            env.Append(CCFLAGS=[
                '-fsanitize=address',
                '-fno-omit-frame-pointer'
            ])
            need_no_omit_fp = False

            # GCC tries to link libasan too early during linking when using the
            # -fsanitize=address option so always add it at the end of the
            # linking command.  Statically linking libasan into a dynamic
            # library does not work either.
            env.Append(SHLINKPOSTFLAGS=['-lasan'])
            env.Append(LINKPOSTFLAGS=['-fsanitize=address'])
            if opt_asan == 'static':
                env.Append(LINKPOSTFLAGS=['-static-libasan'])

    if tsan_supported:
        try:
            SCons.Script.AddOption(
                    '--tsan',
                    help='Use libtsan (no, yes, static) host only, buggy',
                    dest='tsan',
                    type='string',
                    nargs=1,
                    action='store',
                    default='no')
        except (optparse.OptionConflictError):
            # Ignore errors if this is added multiple times.
            pass

        opt_tsan = SCons.Script.GetOption('tsan').lower()
        if opt_tsan in ['yes', 'y', 'static']:
            env.Append(CCFLAGS=['-fsanitize=thread'] + \
                (need_no_omit_fp * ['-fno-omit-frame-pointer']))

            # Both sanitizers are being used.
            if not need_no_omit_fp:
                LogWarn('thread and address sanitizer require different incompatible runtimes')

            env.Append(SHLINKPOSTFLAGS=['-fsanitize=thread', '-ltsan'])
            env.Append(LINKPOSTFLAGS=['-fsanitize=thread', '-ltsan', '-pie'])
            env.Append(CCFLAGS=['-fPIC'])
            if opt_tsan == 'static':
                env.Append(LINKPOSTFLAGS=['-static-libtsan'])
