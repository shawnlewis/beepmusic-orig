import os

import SCons
import beep_scons as beep


def exists(env):
    return True

def SymlinkAction(target, source, env):
    # This is single source so no need to iterate sources.
    target = target[0].abspath
    source = source[0].abspath
    relpath = env.RelativePath(source, target, 1)
    if os.path.exists(target):
        os.remove(target)
    os.symlink(relpath, target)

def add_symlink_builder(env, builder_name):
    # TODO: Still need to figure out how to make a custom decider for this.
    action = SCons.Action.Action(
            SymlinkAction,
            '$SYMCOMSTR')
    builder = SCons.Script.Builder(
            action=action,
            single_source=True,
            target_factory=env.fs.Entry,
            source_factory=env.fs.Entry)
    env.Append(BUILDERS={builder_name: builder})

    if env['VERBOSE_BUILD']:
        env.SetDefault(SYMCOMSTR='creating symlink $TARGETS -> $SOURCES')
    else:
        env.SetDefault(
                SYMCOMSTR='    {} $TARGETS -> $SOURCES'.format(
                        beep.colorstr('LN', bold=True, fg='default-fg')))

def CoffeeGenerator(target, source, env, for_signature):
    target = target[0].abspath
    source = source[0].abspath
    if '-m' in env.get('COFFEEFLAGS', []):
        map_path = SCons.Util.splitext(str(target))[0] + '.map'
        env.SideEffect(map_path, target)
        env.Clean(map_path, target)
    cmd = ['coffee', '$COFFEEFLAGS', '-c', '-o', '$TARGETS.dir', '$SOURCES']
    return [cmd]

def add_coffee_builder(env):
    action = SCons.Action.Action(
            CoffeeGenerator,
            '$COFFEECOMSTR',
            generator=1)
    builder = SCons.Script.Builder(
            action=action,
            single_source=True,
            target_factory=env.fs.Entry,
            source_factory=env.fs.Entry)
    env.Append(BUILDERS={'Coffee': builder})
    #env['COFFEEFLAGS'] = SCons.Util.CLVar('')
    env['COFFEEFLAGS'] = SCons.Util.CLVar('-m')

    if env['VERBOSE_BUILD']:
        env.SetDefault(COFFEECOMSTR='coffee $COFFEEFLAGS -c -o $TARGETS.dir $SOURCES')
    else:
        env.SetDefault(
                COFFEECOMSTR='    {} $TARGETS'.format(
                        beep.colorstr('COFFEE', bold=True, fg='default-fg')))

def LuaGenerator(target, source, env, for_signature):
    target = target[0].abspath
    source = source[0].abspath

    # If previous build made a link for this file remove the link first
    # so we don't overwrite the source.
    if os.path.islink(target) or (os.path.isfile(target)
            and os.stat(target).st_nlink > 1):
        os.remove(target)
    cmd = ['luac', '-o', '$TARGETS', '$SOURCES']
    return [cmd]

def add_lua_builder(env):
    if SCons.Script.GetOption('compile-lua'):
        action = SCons.Action.Action(
                LuaGenerator,
                '$LUACCOMSTR',
                generator=1)
        builder = SCons.Script.Builder(
                action=action,
                single_source=True,
                target_factory=env.fs.Entry,
                source_factory=env.fs.Entry)
        env.Append(BUILDERS={'Lua': builder})

        if env['VERBOSE_BUILD']:
            env.SetDefault(LUACCOMSTR='luac -o $TARGETS $SOURCES')
        else:
            env.SetDefault(
                    LUACCOMSTR='    {} $TARGETS'.format(
                            beep.colorstr('LUAC', bold=True, fg='default-fg')))
    else:
        add_symlink_builder(env, 'Lua')

def generate(env, **kwargs):
    add_symlink_builder(env, 'Symlink')
    add_coffee_builder(env)
    add_lua_builder(env)
