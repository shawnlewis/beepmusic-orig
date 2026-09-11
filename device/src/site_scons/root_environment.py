import collections
from compiler.ast import flatten
import inspect
import os

import SCons


# This file creates a subclass of scons Environment where it can override
# the MethodWrapper, BuilderWrapper and methods to apply target/class/build
# filters on all calls.  This allows for clean targeted based changes to
# environments without bug prone if statements and multiple environments.
# It will also make it easy to build against multiple targets at the same
# time.

# All of the overridden methods are public and the wrappers are closely tied
# to method/builder plugin framework and should not radically change.

# Usage is the same as a normal but filterable calls can add the extra
# argument efilt={<filter key>: <filter value>}.  Omitting efilt will
# apply the call for all targets.

# Example:
# Append CCFLAG -O2 for all targets
#   env.Append(CCFLAGS='-O2')
# Append CCFLAG -O2 for only mytarget target
# env.Append(CCFLAGS='-O2', efilt={'target': 'mytarget'})
# Append CCFLAG -O2 for only mytarget and mytarget2 targets
# env.Append(CCFLAGS='-O2', efilt={'target': ['mytarget', 'mytarget2']})


FILTER_KEY = '_EFILT'
CLONED_ENVS_KEY = '_CLONED_ENVS'
BUILD_TARGETS_KEY = '_BUILD_TARGETS'
BINVER_TARGET_TYPES = ['Program', 'SharedLibrary']


class _Null(object):
    pass

_null = _Null


def env_filter(env, efilt, filt_msg=None):
    """Check if call should be filtered.

    Args:
        env: Environment to apply filters.
        efilt: Filter dict.
    Returns:
        bool: True: If call should be filtered (not a match).
              False: If call should be applied (match).

    By default it is considered a match if any key,value pairs match the
    environment.  This can be changed if efilt contains match-all:True.

    Example:
        If efilt = {
            key1: [value1, value2],
            key2: value3
        }
    This function will return False if key1:value1, key1:value2 or key2:value3
    match the environment or return True if none match.

    Two special cases exist for convenience:
        * Any value of 'all' will always match and return False.
        * efilt=None implies 'all' which will always match and return False.
    """
    if not isinstance(env, RootEnvironment):
        raise Exception('Can only filter on RootEnvironment')

    # No filter key, do not filter (during RootEnvironment.__init__).
    if env.get(FILTER_KEY, None) is None:
        return False

    # No filter requested, do not filter.
    if efilt is None:
        return False

    match_all = not not efilt.pop('match-all', False)
    mismatch = False

    # One of the filters is being applied to all.
    if 'all' in flatten(efilt.values()) and not match_all:
        return False

    for key, efilt_vals in efilt.items():
        env_vals = env[FILTER_KEY].get(key, None)

        if env_vals is None:
            if match_all:
                mismatch = True
                break
            else:
                continue

        if isinstance(efilt_vals, basestring):
            efilt_vals = [efilt_vals,]
        elif not isinstance(efilt_vals, collections.Iterable):
            efilt_vals = list(efilt_vals)

        key_matches = [x in env_vals for x in efilt_vals]
        mismatch = mismatch or not all(key_matches)
        if not match_all and any(key_matches):
            return False

    if not mismatch:
        return False

    # Only create the log message if the logging level is high enough.
    if filt_msg is not None and SCons.Script.GetOption('site_log_level') >= 1:
        # The filtered call is always 2 frames back on the stack.
        calling_stack = inspect.stack()[2]
        d, f = os.path.split(calling_stack[1])
        d = os.path.relpath(d, SCons.Script.Dir('#').abspath)
        f = os.path.join(d, f)
        LogInfo('Filtering: {} from {}:{}'.format(filt_msg, f,
                calling_stack[2]))

    # No matches, filter this.
    return True


class BuildTargetDict(object):
    def __init__(self):
        self._dict = {}

    def __repr__(self):
        ret = 'BuildTargetDict object at {}:'.format(hex(id(self)))
        return ret

    def __str__(self):
        return self.__repr__()

    def _add_target_to_type(self, build_type, name, target):
        type_dict = self._dict.get(build_type, {})
        target_list = type_dict.get(name, [])
        target_list.append(target)
        type_dict[name] = target_list
        self._dict[build_type] = type_dict

    def add_build_target(self, build_type, name, target):
        LogDebug('add_build_target: {} {} type {}'.format(build_type, name,
                type(target)))
        if not isinstance(target, collections.Iterable):
            target = [target]
        for t in target:
            self._add_target_to_type('_all', name, t)
            self._add_target_to_type(build_type, name, t)

    def has_target(self, name):
        return self._dict['_all'].has_key(name)

    def get_target(self, name):
        return self._dict['_all'].get(name, [])

    def list_targets(self, build_type='_all'):
        return sorted(self._dict.get(build_type, {}).keys())

    def get_target_type(self, build_type):
        # Using list targets ensures the ordering is correct (although
        # each name may have multiple targets.
        ret = []
        for x in self.list_targets(build_type):
            ret += self.get_target(x)
        return ret

    def list_target_types(self):
        ret = sorted(self._dict.keys())
        ret.remove('_all')
        return ret


class MethodWrapperFilter(SCons.Environment.MethodWrapper):
    def __call__(self, *args, **kwargs):
        LogDebug('MethodWrapperFilter', self.object, args, kwargs)
        if env_filter(self.object, kwargs.pop('efilt', None),
            'call to \'{}\''.format(self.name)):
            return None

        return super(MethodWrapperFilter, self).__call__(*args, **kwargs)


class BuilderWrapperFilter(SCons.Environment.BuilderWrapper):
    def __call__(self, target=None, source=_null, *args, **kwargs):
        LogDebug('BuilderWrapperFilter', self.object, target, source, args,
                kwargs)

        build_type = self.method.get_name(self.object)

        filt_msg = 'builder \'{}\' target \'{}\''.format(build_type, target)

        if env_filter(self.object, kwargs.pop('efilt', None), filt_msg):
            return SCons.Node.NodeList()

        original_target = target

        keep_source = not not kwargs.pop('keep_source', False)
        keep_target = not not kwargs.pop('keep_target', False)

        # Change the source to point to the variant directories.
        if source != _null and not keep_source:
            if isinstance(source, str):
                source = [source,]
            source = [os.path.join(self.object['TARGET_BUILD_PATH'], x)
                    for x in source]

        # Figure out where to put the target based on what type of builder
        # this is or the output kwarg.
        output = kwargs.pop('output', None)
        if target is not None and not keep_target:
            if output == False or (output == None and \
                    self.method.get_name(self.object) in \
                    self.object.get('BUILDERS_USE_BUILD_PATH', [])):
                target = os.path.join(self.object['TARGET_BUILD_PATH'],
                        target)
            else:
                target = os.path.join(self.object['TARGET_OUTPUT_PATH'],
                        target)

        if build_type in BINVER_TARGET_TYPES and \
                SCons.Script.GetOption('binver_tracking') and \
                kwargs.pop('binver_tracking', True):
            self.object.EmbedVersionInfo(original_target, source,
                    build_type, kwargs)

        node = super(BuilderWrapperFilter, self).__call__(
                target, source, *args, **kwargs)

        self.object[BUILD_TARGETS_KEY].add_build_target(build_type,
                original_target, node)

        return node

    def __repr__(self):
        return '<BuilderWrapperFilter %s>' % repr(self.name)

    def __str__(self):
        return self.__repr__()


class BuilderDictFilter(SCons.Environment.BuilderDict):
    def __setitem__(self, item, val):
        LogDebug('BuilderDictFilter', self.env, item, val)
        try:
            method = getattr(self.env, item).method
        except AttributeError:
            pass
        else:
            self.env.RemoveMethod(method)
        collections.UserDict.__setitem__(self, item, val)
        # Redirect all builders to use the builder wrapper filter.
        BuilderWrapperFilter(self.env, val, item)


class RootEnvironment(SCons.Script.Environment):
    def __init__(self,
            platform=None,
            tools=None,
            toolpath=None,
            variables=None,
            parse_flags=None,
            target_def=None,
            **kw):
        if target_def is None:
            raise Exception('No target definition')

        if tools is None:
            tools = []
        elif isinstance(tools, str):
            tools = [tools,]
        elif not isinstance(tools, list) and \
                isinstance(tools, collections.Iterable):
            tools = [x for x in tools]

        tools.insert(0, ('root_tools', {'target_def': target_def}))

        super(RootEnvironment, self).__init__(
                platform=platform,
                tools=tools,
                toolpath=toolpath,
                variables=variables,
                parse_flags=parse_flags,
                **kw)
        # Redirect addition of all builders (env.Program, env.StaticLibrary,
        # ect...) to the builder dict filter.
        self._dict['BUILDERS'] = BuilderDictFilter(
                self._dict['BUILDERS'], self)

        # Create and empty CLVar for cloned envs.
        self[CLONED_ENVS_KEY] = SCons.Util.CLVar('')

        # The build targets is only created with a new cloned environment.
        # All environments cloned from this will share the same object.  This
        # is fine since all build targets need to have unique paths.
        self[BUILD_TARGETS_KEY] = BuildTargetDict()

    # Redirect plugin methods to use the method wrapper filter.
    def AddMethod(self, function, name=None):
        method = MethodWrapperFilter(self, function, name)
        self.added_methods.append(method)

    # Filter calls to the Environment base.  Not all public calls are here
    # but not implemented.
    def Append(self, **kw):
        if env_filter(self, kw.pop('efilt', None), 'Append'):
            return None
        return super(RootEnvironment, self).Append(**kw)

    def AppendENVPath(self, name, newpath, envname='ENV',
                      sep=os.pathsep, delete_existing=1, efilt=None):
        if env_filter(self, efilt, 'AppendENVPath'):
            return None
        return super(RootEnvironment, self).AppendENVPath(name, newpath,
                envname, sep, delete_existing)

    def AppendUnique(self, delete_existing=0, **kw):
        if env_filter(self, kw.pop('efilt', None), 'AppendUnique'):
            return None
        return super(RootEnvironment, self).AppendUnique(delete_existing,
                **kw)

    # Clone needs to rebind BUILDERS correctly to use the BuilderDictFilter
    # and track the cloned environments for actually invoking scons to build.
    def Clone(self, tools=[], toolpath=None, parse_flags=None, **kw):
        clone = super(RootEnvironment, self).Clone(tools, toolpath,
                parse_flags, **kw)

        clone._dict = SCons.Util.semi_deepcopy_dict(self._dict, ['BUILDERS'])
        clone._dict['BUILDERS'] = BuilderDictFilter(self._dict['BUILDERS'],
                clone)

        # Clear the cloned envs from the new clone
        clone[CLONED_ENVS_KEY] = SCons.Util.CLVar('')

        # Track the new clone in the current environment.
        self.Append(_CLONED_ENVS=clone)

        return clone

    #def Copy(self, *args, **kw):
    #def Decider(self, function):
    #def Detect(self, progs):
    #def Dictionary(self, *args):
    #def Dump(self, key = None):
    #def FindIxes(self, paths, prefix, suffix):
    #def ParseConfig(self, command, function=None, unique=1):
    #def ParseDepends(self, filename, must_exist=None, only_one=0):
    #def Platform(self, platform):

    def Prepend(self, **kw):
        if env_filter(self, kw.pop('efilt', None), 'Prepend'):
            return None
        return super(RootEnvironment, self).Prepend(**kw)

    def PrependENVPath(self, name, newpath, envname='ENV', sep=os.pathsep,
                       delete_existing=1, efilt=None):
        if env_filter(self, efilt, 'PrependENVPath'):
            return None
        return super(RootEnvironment, self).PrependENVPath(name, newpath,
                envname, sep, delete_existing)

    def PrependUnique(self, delete_existing=0, **kw):
        if env_filter(self, kw.pop('efilt', None), 'PrependUnique'):
            return None
        return super(RootEnvironment, self).PrependUnique(delete_existing,
                **kw)

    def Replace(self, **kw):
        if env_filter(self, kw.pop('efilt', None), 'Replace'):
            return None
        return super(RootEnvironment, self).Replace(**kw)

    #def ReplaceIxes(self, path, old_prefix, old_suffix, new_prefix, new_suffix):

    def SetDefault(self, **kw):
        if env_filter(self, kw.pop('efilt', None), 'SetDefault'):
            return None
        return super(RootEnvironment, self).SetDefault(**kw)

    #def Tool(self, tool, toolpath=None, **kw):
    #def WhereIs(self, prog, path=None, pathext=None, reject=[]):

    def Action(self, *args, **kw):
        if env_filter(self, kw.pop('efilt', None), 'Action'):
            return None
        return super(RootEnvironment, self).Action(*args, **kw)

    def AddPreAction(self, files, action, efilt=None):
        if env_filter(self, efilt, 'AddPreAction'):
            return None
        return super(RootEnvironment, self).AddPreAction(files, action)

    def AddPostAction(self, files, action, efilt=None):
        if env_filter(self, efilt, 'AddPostAction'):
            return None
        return super(RootEnvironment, self).AddPostAction(files, action)

    #def Alias(self, target, source=[], action=None, **kw):
    #def AlwaysBuild(self, *targets):
    #def BuildDir(self, *args, **kw):
    #def Builder(self, **kw):
    #def CacheDir(self, path):
    #def Clean(self, targets, files):
    #def Configure(self, *args, **kw):

    def Command(self, target, source, action, **kw):
        if env_filter(self, kw.pop('efilt', None), 'Command'):
            return None
        return super(RootEnvironment, self).Command(target, source, action,
                **kw)

    def Depends(self, target, dependency, efilt=None):
        if env_filter(self, efilt, 'Depends'):
            return None
        return super(RootEnvironment, self).Depends(target, dependency)

    #def Dir(self, name, *args, **kw):
    #def NoClean(self, *targets):
    #def NoCache(self, *targets):
    #def Entry(self, name, *args, **kw):
    #def Environment(self, **kw):
    #def Execute(self, action, *args, **kw):
    #def File(self, name, *args, **kw):
    #def FindFile(self, file, dirs):
    #def Flatten(self, sequence):
    #def GetBuildPath(self, files):
    #def Glob(self, pattern, ondisk=True, source=False, strings=False):

    def Ignore(self, target, dependency, efilt=None):
        if env_filter(self, efilt, 'Ignore'):
            return None
        return super(RootEnvironment, self).Ignore(target, dependency)

    #def Literal(self, string):
    #def Local(self, *targets):
    #def Precious(self, *targets):
    #def Repository(self, *dirs, **kw):
    #def Requires(self, target, prerequisite):
    #def Scanner(self, *args, **kw):
    #def SConsignFile(self, name=".sconsign", dbm_module=None):
    #def SideEffect(self, side_effect, target):
    #def SourceCode(self, entry, builder):
    #def SourceSignatures(self, type):
    #def Split(self, arg):
    #def TargetSignatures(self, type):
    #def Value(self, value, built_value=None):
    #def VariantDir(self, variant_dir, src_dir, duplicate=1):
    #def FindSourceFiles(self, node='.'):
    #def FindInstalledFiles(self):

    # This is not part of Environment base but is cleaner if it is part of
    # class and not an external method.
    def Filter(self, efilt, filt_msg=None):
        return env_filter(self, efilt, filt_msg)
