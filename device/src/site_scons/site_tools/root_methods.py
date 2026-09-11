import os

import SCons
import beep_scons as beep
from root_environment import CLONED_ENVS_KEY


BINVER_SOURCE = 'lib/beep/beep_ver.c'
BINVER_SYMBOL = 'beep_version'


def exists(env):
    return True

# Usage: root_env.FilterOut(CPPDEFINES=['flag1', 'flag2'], CCFLAGS=['flag3'])
def FilterOut(env, **kwargs):
    # kwargs = {'CCFLAGS': ['flag3'], 'CPPDEFINES': ['flag1', 'flag2'] }
    # Don't remove important things.
    kwargs = SCons.Environment.copy_non_reserved_keywords(kwargs)
    for key, filter_vals in kwargs.items():
        env_vals = env.get(key, None)
        if env_vals is None:
            continue
        for val in filter_vals:
            while val in env_vals:
                env_vals.remove(val)
    env[key] = env_vals

# Usage: root_env.RelativePath('/a/b/c.txt', '/a/d')
# Usage: root_env.RelativePath('/a/b/c.txt', '/a/d/c.txt', 1)
# Returns: '../b/c.txt'
# Note: path and start may be strings or Nodes.
def RelativePath(env, path, start, start_trim=0):
    path = env.Entry(str(path)).abspath
    start = env.Entry(str(start)).abspath
    if start_trim:
        start = start.split(os.sep)
        if len(start) <= start_trim:
            raise Exception('cannot trim path {} value {}'.format(
                    os.sep.join(start), start_trim))
        start = os.sep.join(start[:start_trim * -1])
    return os.path.relpath(path, start)

# Same as env[k] = v but useful when using filters.
def Set(env, **kw):
    for k,v in kw.items():
        env[k] = SCons.Util.CLVar(v)

def flatten_cloned_envs(env):
    for cloned_env in env.get(CLONED_ENVS_KEY, []):
        if len(cloned_env.get(CLONED_ENVS_KEY, [])):
            for x in flatten_cloned_envs(cloned_env):
                yield x
        else:
            yield cloned_env
    yield env

# Recursively get all environments cloned from this env and this env.
def ClonedEnvs(env):
    return [x for x in flatten_cloned_envs(env)]

def EmbedVersionInfo(env, original_target, source, build_type, kwargs):
    #obj_target = os.path.join(env['TARGET_BUILD_PATH'], 'binver',
    #        original_target.replace('/', '__'))
    obj_target = os.path.join('binver', original_target.replace('/', '__'))
    if build_type == 'SharedLibrary':
        obj_target += '_so'
        obj = env.SharedObject(
                target=obj_target,
                source=BINVER_SOURCE,
                CPPDEFINES=binver_defines)
    else:
        obj = env.Object(
                target=obj_target,
                source=BINVER_SOURCE,
                CPPDEFINES=binver_defines)
    source.append(obj)
    lf = kwargs.get('LINKFLAGS', env.get('LINKFLAGS', {}))
    undef_arg = '-Wl,--undefined={}'.format(BINVER_SYMBOL)
    if undef_arg not in lf:
        if isinstance(lf, basestring):
            lf += ' ' + undef_arg
        else:
            lf.append(undef_arg)

# Add pretty post action.
def AddPPostAction(env, files, action, comm=None, args=None):
    # efilt would have already been checked by MethodWrapperFilter so omit
    # it when calling AddPostAction.
    if comm is not None and not env['VERBOSE_BUILD']:
        action = SCons.Action.Action(
                action,
                beep.pretty_com_str(comm, args))
    env.AddPostAction(files, action)

def generate(env, **kwargs):
    env.AddMethod(FilterOut)
    env.AddMethod(RelativePath)
    env.AddMethod(Set)
    env.AddMethod(ClonedEnvs)
    env.AddMethod(EmbedVersionInfo)
    env.AddMethod(AddPPostAction)
