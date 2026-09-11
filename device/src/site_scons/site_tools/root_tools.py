import optparse
import os
import platform
import sys

import SCons
import beep_scons as beep
from root_environment import FILTER_KEY


def exists(env):
    return True

def generate(env, **kwargs):
    target_def = kwargs['target_def']

    env[FILTER_KEY] = {
        'target': SCons.Util.CLVar(target_def.name),
        'arch': SCons.Util.CLVar(target_def.arch),
        'endianness': SCons.Util.CLVar(target_def.endianness),
        'device_type': SCons.Util.CLVar(target_def.device_type),
    }

    # Set commonly used defaults to empty CLVars that are not by default
    # (do this before loading any other tools).
    env.SetDefault(
            CPPPATH=SCons.Util.CLVar(''),
            LIBPATH=SCons.Util.CLVar(''),
            RPATH=SCons.Util.CLVar(''))

    # Set verbose build before adding other builders.
    env.SetDefault(VERBOSE_BUILD=False)
    for key, val in SCons.Script.ARGLIST:
        if key == 'V' and val != '':
            env['VERBOSE_BUILD'] = True
            break
    if SCons.Script.GetOption('verbose') == True:
        env['VERBOSE_BUILD'] = True

    # Add scons default tools.
    env.Tool('default')
    # Add custom methods and builders for all targets.
    env.Tool('root_methods')
    env.Tool('root_builders')

    # Set some defaults on how we build different targets.
    env.SetDefault(
            TARGET_NAME=target_def.name,
            OUTPUT_PATH=os.path.join('#/out'),
            BUILD_PATH=os.path.join('#/build'),
            TARGET_OUTPUT_PATH=os.path.join('#/out', target_def.name),
            TARGET_BUILD_PATH=os.path.join('#/build', target_def.name),
            BUILDERS_USE_BUILD_PATH=SCons.Util.CLVar([
                'CFile',
                'CXXFile',
                'Library',
                'Object',
                'SharedObject',
                'StaticLibrary',
                'StaticObject',]),
            SYSROOT='')

    # Need to set this since it will be set but not used by scons.
    env['TARGET_ARCH'] = target_def.arch

    # Add SHLINKPOSTFLAGS and LINKPOSTFLAGS.  The standard (SH)LINKFLAGS come
    # before the sources and libdir flags which is not useful for linking
    # global libraries.
    env.Append(
            LINKCOM=' $LINKPOSTFLAGS',
            SHLINKCOM=' $SHLINKPOSTFLAGS')
    env.SetDefault(
            LINKPOSTFLAGS=SCons.Util.CLVar(''),
            SHLINKPOSTFLAGS=SCons.Util.CLVar(''))

    # Setup cpp defines for BEEP_DEVICE and BEEP_VIRTUAL.
    if target_def.device_type == 'real':
        env.Append(CPPDEFINES={'BEEP_DEVICE':1})
    if target_def.device_type == 'virtual':
        env.Append(CPPDEFINES={'BEEP_VIRTUAL':1})

    # Don't rescan the file if the timestamp hasn't changed.
    env.Decider('MD5-timestamp')

    # Setup the target variant directory.
    env.VariantDir(env.get('TARGET_BUILD_PATH'), SCons.Script.Dir('#'))

    # Add output and builder path to linker paths.
    env.Append(LIBPATH=[env['TARGET_BUILD_PATH'], env['TARGET_OUTPUT_PATH']])

    # Load the toolchain modifications after defaults have been setup.
    toolchain = SCons.Script.GetOption('toolchain') or \
            target_def.toolchain
    if isinstance(toolchain, basestring):
        try:
            env.Tool('toolchain_' + toolchain)
        except SCons.Errors.EnvironmentError:
            if SCons.Script.GetOption('toolchain') == toolchain:
                LogError('Could not load toolchain: \'{}\''.format(
                        toolchain))
                sys.exit(2)
            else:
                LogWarn('Could not load target defined toolchain: \'{}\'' \
                        .format(toolchain))

    try:
        SCons.Script.AddOption(
                '--gc-sections',
                help='Remove unused functions and globals',
                dest='gc-sections',
                action='store_true',
                default=False)
        SCons.Script.AddOption(
                '--print-gc-sections',
                help='Print unused functions and globals being removed',
                dest='print-gc-sections',
                action='store_true',
                default=False)
        SCons.Script.AddOption(
                '--save-temps',
                help='Save gcc temporary files to object directory',
                dest='save-temps',
                action='store_true',
                default=False)
    except (optparse.OptionConflictError):
        # Ignore errors if this is added multiple times.
        pass

    # Add command line modifications last.
    if SCons.Script.GetOption('gc-sections'):
        env.Append(CCFLAGS=['-ffunction-sections', '-fdata-sections'])
        env.Append(LINKFLAGS='-Wl,--gc-sections')

    if SCons.Script.GetOption('print-gc-sections'):
        env.Append(LINKFLAGS='-Wl,--print-gc-sections')

    if SCons.Script.GetOption('save-temps'):
        env.Append(CCFLAGS=['-save-temps=obj'])

    # Make nice output for the default builders if verbose is off.
    if not env['VERBOSE_BUILD']:
        env.SetDefault(
                ARCOMSTR=beep.pretty_com_str('AR', '$TARGET'),
                ASCOMSTR=beep.pretty_com_str('AS', '$TARGET'),
                CCCOMSTR=beep.pretty_com_str('CC', '$TARGET'),
                CXXCOMSTR=beep.pretty_com_str('CXX', '$TARGET'),
                LINKCOMSTR=beep.pretty_com_str('LINK', '$TARGET'),
                RANLIBCOMSTR=beep.pretty_com_str('RANLIB', '$TARGET'),
                SHCCCOMSTR=beep.pretty_com_str('CC', '$TARGET'),
                SHCXXCOMSTR=beep.pretty_com_str('CXX', '$TARGET'),
                SHLINKCOMSTR=beep.pretty_com_str('LINK', '$TARGET'),
                SYMCOMSTR=beep.pretty_com_str('LN', '$SOURCES -> $TARGETS'))
