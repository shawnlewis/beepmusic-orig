import __builtin__
import multiprocessing
import sys

import root_environment
import beep_scons as beep


def BuildEnvs(env_list):
    # Need to use __builtin__.LogInfo since it may be LogNull depending
    # on logging level.
    __builtin__.LogInfo('building environments')

    # Check for list_build_targets first since this will exit out.
    if SCons.Script.GetOption('list_build_targets'):
        all_targets = []
        for env in env_list:
            all_targets += \
                    env[root_environment.BUILD_TARGETS_KEY].list_targets()
        all_targets = sorted(list(set(all_targets)))
        print 'Build target list:'
        print '\n'.join(all_targets)
        sys.exit(0)

    # Check for clean targets and return since scons will to the actual
    # cleaning.
    if 'clean' in COMMAND_LINE_TARGETS:
        SCons.Script.Default(None)
        SCons.Script.BUILD_TARGETS = [SCons.Script.Dir('#')]
        for env in env_list:
            SCons.Script.Clean(SCons.Script.Dir('#'),
                    env['TARGET_OUTPUT_PATH'] + '/')
            SCons.Script.Clean(SCons.Script.Dir('#'),
                    env['TARGET_BUILD_PATH'] + '/')
        SCons.Script.SetOption('clean', True)
        return

    if 'distclean' in COMMAND_LINE_TARGETS:
        SCons.Script.Default(None)
        SCons.Script.BUILD_TARGETS = [SCons.Script.Dir('#')]
        SCons.Script.Clean(SCons.Script.Dir('#'), env_list[0]['OUTPUT_PATH'])
        SCons.Script.Clean(SCons.Script.Dir('#'), env_list[0]['BUILD_PATH'])
        SCons.Script.Clean(SCons.Script.Dir('#'), __builtin__.binver_cache_path)
        SCons.Script.SetOption('clean', True)
        return

    if len(COMMAND_LINE_TARGETS):
        SCons.Script.Default(None)
        # Clear the build targets setup by scons.
        SCons.Script.BUILD_TARGETS = []

        build_target_dicts = [x[root_environment.BUILD_TARGETS_KEY] for x in
                env_list]
        # If any command line targets exist in the environments create a
        # target to the environment variant.  If it is not found copy it back
        # to build target list for scons.
        for cmd_target in COMMAND_LINE_TARGETS:
            targets = [x.get_target(cmd_target) for x in build_target_dicts]
            targets = [x for x in targets if x != []]
            if len(targets):
                for t in targets:
                    SCons.Script.BUILD_TARGETS += t
            else:
                SCons.Script.BUILD_TARGETS.append(cmd_target)


def MainSConscript(env_list, script_path, efilt=None):
    for root_env in env_list:
        if root_env.Filter(efilt, 'MainSConscript \'{}\''.format(script_path)):
            continue

        env = root_env.Clone()
        exports = {
            'root_env': root_env,
            'env': env
        }
        SConscript(script_path, exports=exports)

def LogError(*args):
    beep.log(beep.LOG_LEVEL_ERROR, args)

def LogWarn(*args):
    beep.log(beep.LOG_LEVEL_WARN, args)

def LogInfo(*args):
    beep.log(beep.LOG_LEVEL_INFO, args)

def LogDebug(*args):
    beep.log(beep.LOG_LEVEL_DEBUG, args)

def LogNull(*args):
    pass

def site_init_main():
    if hasattr(__builtin__, 'LogError'):
        return

    __builtin__.LogError = LogError
    __builtin__.LogWarn = LogWarn

    # Default these logging levels are off.
    __builtin__.LogInfo = LogNull
    __builtin__.LogDebug = LogNull

    __builtin__.RootEnvironment = root_environment.RootEnvironment

    # Need to add the target and toolchain options here so they are
    # loaded before any tools are created.
    SCons.Script.AddOption(
            '--target',
            help='Target platform(s)',
            dest='target',
            type='string',
            nargs=1,
            action='store',
            default='host')
    SCons.Script.AddOption(
            '--toolchain',
            help='Toolchain selection (overrides target def toolchain)',
            dest='toolchain',
            type='string',
            nargs=1,
            action='store',
            default=None)
    SCons.Script.AddOption(
            '--list-targets',
            help='List targets and exit',
            dest='list_targets',
            action='store_true',
            default=False)
    SCons.Script.AddOption(
            '--list-toolchains',
            help='List toolchains and exit',
            dest='list_toolchains',
            action='store_true',
            default=False)
    # This option should be checked in BuildEnvs to ensure all targets have
    # been added.
    SCons.Script.AddOption(
            '--list-build-targets',
            help='List build targets and exit',
            dest='list_build_targets',
            action='store_true',
            default=False)
    SCons.Script.AddOption(
            '--verbose',
            help='Verbose build output',
            dest='verbose',
            action='store_true',
            default=False)
    SCons.Script.AddOption(
            '--site-log',
            help='site_scons logging level (0,1,2)',
            dest='site_log_level',
            type='int',
            nargs=1,
            action='store',
            default=0,
            metavar='LEVEL')
    SCons.Script.AddOption(
            '--binver-cache',
            help='Binary version cache usage (no, yes, store)',
            dest='binver_cache',
            type='string',
            nargs=1,
            action='store',
            default='no',
            metavar='ACTION')
    SCons.Script.AddOption(
            '--no-binver-tracking',
            help='Disable embedding version info in binaries',
            dest='binver_tracking',
            action='store_false',
            default=True)
    SCons.Script.AddOption(
            '--compile-lua',
            help='Compile lua code with luac',
            dest='compile-lua',
            action='store_true',
            default=False)


    if SCons.Script.GetOption('list_targets'):
        beep.list_targets()
        sys.exit(0)

    if SCons.Script.GetOption('list_toolchains'):
        beep.list_toolchains()
        sys.exit(0)

    # Needed by distclean so always set this.
    binver_cache_path = os.path.join(SCons.Script.Dir('#').abspath,
            '.binver_cache')
    __builtin__.binver_cache_path = binver_cache_path

    if SCons.Script.GetOption('binver_tracking'):
        # The binver cache does not need to be done this early but it's easier
        # to deal with here instead of after environment creation and keep
        # just a global instance of it.
        opt_binver_cache = SCons.Script.GetOption('binver_cache').lower()
        if opt_binver_cache in ['yes', 'y']:
            binver_defines = beep.load_binver_defines(binver_cache_path)
        else:
            binver_defines = beep.get_binver_defines()
            if opt_binver_cache == 'store':
                beep.save_binver_defines(binver_defines, binver_cache_path)
                sys.exit(0)

        if binver_defines in [None, {}]:
            LogError('Binary version information not found ' +
                    '(use --no-binver-tracking to disable).')
            sys.exit(2)

        __builtin__.binver_defines = binver_defines

    # Set default jobs to number cores (including hyper-threading).
    # This can be overridden by the -j switch.
    SetOption('num_jobs', multiprocessing.cpu_count())

    if SCons.Script.GetOption('site_log_level') >= 1:
        __builtin__.LogInfo = LogInfo
    if SCons.Script.GetOption('site_log_level') >= 2:
        __builtin__.LogDebug = LogDebug


site_init_main()
