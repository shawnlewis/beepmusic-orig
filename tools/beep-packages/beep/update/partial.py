import commands
import magic
import os
import re

import beep.binutils
from beep.utils import quick_cmd

def _path_type(path):
    # Need to do islink first since isfile and isdir will follow symbolic
    # links.
    if os.path.islink(path):
        return 'l'
    elif os.path.isfile(path):
        return 'f'
    elif os.path.isdir(path):
        return 'd'

    raise Exception('Unknown type: {}'.format(path))

def _gen_diff_dict(a_path, b_path):
    ret = {
        'dif': [],
        'add': [],
        'del': []
    }

    a_root = a_path
    b_root = b_path

    if not a_root.endswith(os.path.sep):
        a_root += os.path.sep
    if not b_root.endswith(os.path.sep):
        b_root += os.path.sep

    cmd = 'diff -rq --no-dereference {} {}'.format(a_path, b_path)
    s, raw_diff = commands.getstatusoutput(cmd)

    only_in_a = 'Only in {}'.format(a_root)
    only_in_b = 'Only in {}'.format(b_root)

    for line in raw_diff.split('\n'):
        # This parsing is only tested on diffutils 3.3.
        if line.startswith('Files ') and line.endswith(' differ'):
            # Files <path> and <path> differ
            pattern = '^Files {}(.*?) and {}(.*?) differ$'.format(
                    a_root, b_root)
            m = re.search(pattern, line)
            # dif is only for different files that exist in a_path and b_path.
            # This is used later to determine if the only difference is version
            # information in the .beep section in which case the file does not
            # need to be updated.
            a_diff = m.group(1)
            b_diff = m.group(2)
            if a_diff != b_diff:
                raise Exception('path: {} and {} do not match'.format(
                        a_diff, b_diff))
            #print('dif: {}'.format(b_diff))
            ret['dif'].append(b_diff)
        elif line.startswith('Symbolic links') and line.endswith(' differ'):
            # Symbolic link <path> and <path> differ
            # Links can not be changed.  This needs to create a delete/add to
            # work.
            pattern = '^Symbolic links {}(.*?) and {}(.*?) differ$'.format(
                    a_root, b_root)
            m = re.search(pattern, line)
            a_diff = m.group(1)
            b_diff = m.group(2)
            if a_diff != b_diff:
                raise Exception('path: {} and {} do not match'.format(
                        a_diff, b_diff))
            #print('del: {}'.format(a_diff))
            #print('add: {}'.format(b_diff))
            ret['del'].append(a_diff)
            ret['add'].append(b_diff)
        elif line.startswith('File '):
            # File <path> is a <type> while file <path> is a <type>
            # <type>:
            #   'directory'
            #   'symbolic link'
            #   'regular file'
            # This covers the same path but changing from one type to another.
            # To be safe create a delete entry for a and add entry for b.
            pattern = '^File {}(.*?) is a (regular file|directory|symbolic ' \
                    'link) while file {}(.*?) is a (regular file|directory|' \
                    'symbolic link)$'.format(a_root, b_root)
            m = re.search(pattern, line)
            a_diff = m.group(1)
            b_diff = m.group(3)
            if a_diff != b_diff:
                raise Exception('path: {} and {} do not match'.format(
                        a_diff, b_diff))
            #print('del: {}'.format(a_diff))
            #print('add: {}'.format(b_diff))
            ret['del'].append(a_diff)
            ret['add'].append(b_diff)
        elif line.startswith(only_in_a):
            # Only in <directory>: <file>
            pattern = '^Only in {}(.*?): (.*?)$'.format(a_root)
            m = re.search(pattern, line)
            a_diff = os.path.join(m.group(1), m.group(2))
            #print('del: {}'.format(a_diff))
            ret['del'].append(a_diff)
        elif line.startswith(only_in_b):
            # Only in <directory>: <file>
            pattern = '^Only in {}(.*?): (.*?)$'.format(b_root)
            m = re.search(pattern, line)
            b_diff = os.path.join(m.group(1), m.group(2))
            #print('add: {}'.format(b_diff))
            ret['add'].append(b_diff)
        elif line == '':
            pass
        else:
            raise Exception('Unknown diff line: {}'.format(line))

    return ret

def _dump_diff_dict(diff_dict):
    for k in diff_dict:
        print(k)
        for v in diff_dict[k]:
            print('  {}'.format(v))

# Note this is destructive to the contents of a_path and b_path.
def fs_diff(a_path, b_path):
    if not os.path.isdir(a_path):
        raise Exception('a_path must be directory')
    if not os.path.isdir(b_path):
        raise Exception('b_path must be directory')

    # For stage 1 need to find which files originally differ.
    diff_dict = _gen_diff_dict(a_path, b_path)

    # For each dif file check if it's a binary (executable or shared library)
    # and if it has the .beep section.  If so remove it from both the a_path
    # and b_path.
    for path in diff_dict['dif']:
        a_file = os.path.join(a_path, path)
        b_file = os.path.join(b_path, path)

        a_magic = magic.from_file(a_file)
        b_magic = magic.from_file(b_file)
        if a_magic.startswith('ELF') and a_magic == b_magic:
            #print('{} is a binary'.format(path))
            a_secs = beep.binutils.list_sections(a_file)
            b_secs = beep.binutils.list_sections(b_file)
            if '.beep' in a_secs and '.beep' in b_secs:
                #print('{} is a beep binary'.format(path))
                beep.binutils.del_section(a_file, '.beep')
                beep.binutils.del_section(b_file, '.beep')

    # For stage 2 check for different binaries now that the .beep section
    # has been removed.
    diff_dict = _gen_diff_dict(a_path, b_path)

    return diff_dict
