import fnmatch
import functools
import os
import shutil
import tempfile

from beep.update.manifest import Manifest
import beep.update
from beep.update.constants import *
from beep.utils import quick_cmd

def files_cmp(a, b):
    a = a.split('/')
    b = b.split('/')
    while len(a) and len(b):
        x = a.pop(0)
        y = b.pop(0)
        if x != y:
            break
    if not len(a) or not len(b):
        return cmp(len(a), len(b))
    return cmp(x,y)

class Bpk(object):
    # It is best to just set the root from the start and not allow
    # the user to change it.  It would not make sense to change the root
    # and later break a bunch of rules.

    # Note this creates files in the root during generation to avoid
    # having to do --transforms in tar for each file.  An alternative
    # is to *copy* everything to a temp directory.  This is important
    # as tar packs up hardlinks without following them.
    def __init__(self, root, key_paths):
        if not os.path.isdir(root):
            raise Exception('root: {} must be a directory'.format(root))
        self.root = os.path.realpath(os.path.expanduser(root))
        if self.root[-1] != '/':
            self.root += '/'

        # Get the full path for the key files before we start messing
        # around with the cwd.
        self.key_paths = [os.path.abspath(os.path.expanduser(x)) for x
                in key_paths]

        # Setup default signing methods.
        self.files_sign_method = 'md5'  # Method used in manifest.yml
        self.manifest_sign_method = 'md5'  # Method used to sign manifest.yml
        # Method used to sign output bpk
        self.package_sign_method = 'signed-sha256'
        self.script_sign_method = 'md5'  # Method used to sign every script

        # Track files, scripts, and removes separately instead of in a
        # manifest since it will be easier to sort them later.
        # Do not allow the user to access these based on index since
        # we are going to change them at will.
        self.files = []
        self.pre_scripts = []
        self.post_scripts = []
        self.removes = []
        # This is the most universal remove flags, but the most dangerous if
        # used incorrectly.
        self.remove_flags = \
            REMOVE_FLAGS['CONT_ON_ERR'] \
            | REMOVE_FLAGS['ALLOW_DIRS'] \
            | REMOVE_FLAGS['ALLOW_RESURSIVE']

    def _path_from_root(self, path):
        cwd = os.getcwd()
        os.chdir(self.root)
        path = os.path.abspath(os.path.expanduser(path))
        os.chdir(cwd)
        if not path.startswith(self.root):
            return None
        # Trim root from path.
        path = path[len(self.root):]
        return path

    def print_summary(self):
        print('root: {}'.format(self.root))
        print('files:')
        for f in self.files:
            print('    {}'.format(f))
        print('pre scripts:')
        for s in self.pre_scripts:
            print('    {}'.format(s))
        print('post scripts:')
        for s in self.post_scripts:
            print('    {}'.format(s))
        print('removes:')
        for r in self.removes:
            print('    {}'.format(r))
        print('')

    def add_file(self, path):
        path = self._path_from_root(path)
        if path is None:
            raise Exception('path \'{}\' is not part of root \'{}\''.format(
                    path, self.root))
        if path not in self.files:
            self.files.append(path)

    def add_files_all(self):
        cwd = os.getcwd()
        os.chdir(self.root)
        files = quick_cmd('find . -type f -or -type l').split('\n')
        files = [x for x in files if x != '']
        for f in files:
            self.add_file(f)
        os.chdir(cwd)

    def add_files_fnmatch(self, pattern):
        cwd = os.getcwd()
        os.chdir(self.root)
        files = quick_cmd('find . -type f -or -type l').split('\n')
        files = fnmatch.filter(files, pattern)
        for f in files:
            self.add_file(f)
        os.chdir(cwd)

    def del_file(self, path):
        path = self._path_from_root(path)
        if path in self.files:
            self.files.remove(path)

    def has_file(self, path):
        return path in self.files

    def _script_exists(self, scripts, path, engine):
        return (path, engine) in scripts

    def add_script(self, path, engine, when, exclude_from_files=True):
        """Add a script

        Args:
            path: path to the script (does not need to be in root)
            engine: type of script ['sh', 'lua']
            when: when to run the script ['pre', 'post']
            exclude_from_files: If True the path will be removed from files
                if it exists.  False it will not.  A script can be added back
                to the file list after calling this.
        """
        if engine not in ['sh', 'lua']:
            raise ValueError('engine must be \'sh\' or \'lua\'')
        if when not in ['pre', 'post']:
            raise ValueError('when must be \'pre\' or \'post\'')

        if when == 'pre':
            scripts = self.pre_scripts
        else:
            scripts = self.post_scripts

        # Note: script paths are sourced from where the script is run,
        # not where the root is.
        path = os.path.abspath(path)

        if not self._script_exists(scripts, path, engine):
            scripts.append((path, engine))

        # Using the abspath here will still allow it to be trimmed from
        # the root if exclude_from_files is True.
        if exclude_from_files:
            path = self._path_from_root(path)
            if path is not None:
                self.del_file(path)

    def del_script(self, path, engine, when):
        if engine not in ['sh', 'lua']:
            raise ValueError('engine must be \'sh\' or \'lua\'')
        if when not in ['pre', 'post']:
            raise ValueError('when must be \'pre\' or \'post\'')

        if when == 'pre':
            scripts = self.pre_scripts
        else:
            scripts = self.post_scripts

        if self._script_exists(scripts, path, engine):
            scripts.remove((path, engine))

    def add_remove(self, path):
        # This removes all leading '.' or '/', beepupdate will reference this
        # from sysconfig->prefix.
        path = path.lstrip('./')
        if path not in self.removes:
            self.removes.append(path)

    def del_remove(self, path):
        path = path.lstrip('./')
        if path in self.removes:
            self.removes.append(path)

    def _gen_bpk_prereq(self):
        cwd = os.getcwd()
        os.chdir(self.root)
        # Check for bpktool reserved files.
        reqpath = None
        for path in ['./manifest.yml', './SCRIPTS']:
            if os.path.exists(path):
                reqpath = path
                break
        os.chdir(cwd)

        if reqpath is not None:
            raise Exception('{} is needed to create package'.format(reqpath))

    def _bpk_script_path(self, path, engine, when):
        # The script path inside of the tar file is:
        # SCRIPTS/<md5(path, engine, when)>-<script name>
        # Using md5 instead of something random to ensure reproducibility.
        script_name = os.path.split(path)[1]
        md5 = quick_cmd('echo {} | openssl md5'.format(path + engine + when))
        md5 = md5.split(' ')[1]
        return os.path.join('SCRIPTS', '{}-{}'.format(md5, script_name))

    def _process_scripts(self, man, scripts, when):
        for path, engine in scripts:
            # Only create the SCRIPTS directory if we're actually using
            # a script.
            if not os.path.exists('./SCRIPTS'):
                os.mkdir('SCRIPTS')
            # Script path from bpk root.
            bpk_path = self._bpk_script_path(path, engine, when)

            # Create and embed the digest (engine is 'lua' or 'sh' so it
            # is the same as style.
            digest = beep.update.verify.Digest(path, engine)
            digest.add(self.script_sign_method)
            digest.embed(path=bpk_path)

            # Add script to the manifest.
            man.append(engine, bpk_path, method=self.files_sign_method)

    def sort_files(self):
        # Sort files to sort all files before sub-directories.
        self.files.sort(key=functools.cmp_to_key(files_cmp))
        # Sort removes to sort all sub-directories before files.
        self.removes.sort(key=functools.cmp_to_key(files_cmp))
        self.removes.reverse()

    def gen_bpk(self, bpk_path, sign_bpk=True, sort_files=True):
        self._gen_bpk_prereq()

        if sort_files is True:
            self.sort_files()

        man = Manifest()

        # Get full output path before we change cwd.
        bpk_path = os.path.abspath(bpk_path)

        # Change to the root for creating the bpk.
        cwd = os.getcwd()
        os.chdir(self.root)

        # Order for the manifest:
        #   pre scripts
        #   removes
        #   files
        #   post scripts
        self._process_scripts(man, self.pre_scripts, 'pre')

        for path in self.removes:
            man.append('rm', path, flags=self.remove_flags)

        for path in self.files:
            man.append('file', path, method=self.files_sign_method)

        self._process_scripts(man, self.post_scripts, 'post')

        # Create and sign the manifest.
        man_f = open('manifest.yml', 'w')
        man_f.write(str(man))
        man_f.close()
        digest = beep.update.verify.Digest('manifest.yml', 'yml')
        digest.add(self.manifest_sign_method)
        digest.embed()

        # Create a temp file that lists all of the files we will be
        # putting into the tar file.
        tar_list_fd, tar_list_path = tempfile.mkstemp()
        tar_list_f = os.fdopen(tar_list_fd, 'w')

        # The manifest is always the first file.
        tar_list_f.write('manifest.yml\n')

        for ftype, path in man.list_type_path():
            # Add all files in the manifest except rm.
            if ftype != 'rm':
                tar_list_f.write('{}\n'.format(path))

        tar_list_f.close()

        # Create the tar.gz.
        quick_cmd('tar --group=root --owner=root ' \
                '--files-from={} -czf {}'.format(tar_list_path, bpk_path))
        if sign_bpk is True:
            digest = beep.update.verify.Digest(bpk_path, 'bin',
                    key_paths=self.key_paths)
            digest.add(self.package_sign_method)
            digest.embed(verbose=True)

        # Cleanup
        os.unlink('manifest.yml')
        os.unlink(tar_list_path)
        if os.path.exists('SCRIPTS'):
            shutil.rmtree('SCRIPTS')

        # Leave the package root.
        os.chdir(cwd)

        # Sure why not.
        return man
