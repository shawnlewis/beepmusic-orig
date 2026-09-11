#!/usr/bin/env python

import os
import sys
sys.path.insert(1, os.path.join(os.path.realpath(os.path.split(__file__)[0]), 'beep-packages'))

import argparse
import fnmatch
import shutil
import tempfile
import textwrap
import time

import beep.update
from beep.update.constants import *
from beep.utils import quick_cmd

try:
    import argcomplete
except:
    pass

# fnmatch patterns.
PARTIAL_IGNORE_RULES = {
    'dif': [
        '/beep/beepupdate',
        'usr/lib/opkg/*'
    ],
    'add': [
        '/beep/beepupdate',
        'usr/lib/opkg/*'
    ],
    'del': [
        '/beep/beepupdate'
    ]
}

debug = 0

def dprint(s, l=1):
    if debug >= l:
        print(s)

def sign_cmd(args):
    if not os.path.isfile(args.inpath):
        raise Exception('\'{}\' is not a file'.format(args.inpath))

    outpath = args.outpath or args.inpath

    digest = beep.update.verify.Digest(args.inpath, args.file_type,
            key_paths=args.key_path)
    digest.add(args.method)
    digest.embed(outpath)

def del_sig_cmd(args):
    if not os.path.isfile(args.inpath):
        raise Exception('\'{}\' is not a file'.format(args.inpath))
    outpath = args.outpath or args.inpath
    beep.update.verify.del_signature(args.inpath, outpath)

def create_bpk_cmd(args):
    # Create bpk with a root in the current directory and add all files.
    bpk = beep.update.bpk.Bpk(os.getcwd(), args.key_file)
    bpk.add_files_all()

    # Add scripts specified from the command line.
    # format: [engine, path, when]
    pre = [x.split(':', 1) + ['pre'] for x in (args.prescript or [])]
    post = [x.split(':', 1) + ['post'] for x in (args.postscript or [])]

    for engine, path, when in pre + post:
        bpk.add_script(path, engine, when)

    # Add any removes
    for rpath in (args.remove or []):
        bpk.add_remove(rpath)

    man = bpk.gen_bpk(args.output)

    print('Manifest for {}'.format(os.path.abspath(args.output)))
    print(str(man))

def partial_update_cmd(args):
    if not beep.update.uboot.verify_image(args.image1):
        raise Exception('Image verify failed on: {}'.format(args.image1))
    if not beep.update.uboot.verify_image(args.image2):
        raise Exception('Image verify failed on: {}'.format(args.image2))

    hdr1 = beep.update.uboot.img_header(args.image1)
    hdr2 = beep.update.uboot.img_header(args.image2)
    timestr1 = time.strftime('%Y%m%d %H:%M:%S', time.localtime(hdr1['time']))
    timestr2 = time.strftime('%Y%m%d %H:%M:%S', time.localtime(hdr2['time']))

    print('Creating partial update bpk from:')
    print('    Base image: {} built on {}'.format(args.image1, timestr1))
    print('    New image: {} built on {}'.format(args.image2, timestr2))

    tempdir = tempfile.mkdtemp(prefix='tmpbpk')

    # Extract the old image, two copies of the new image and a root to create
    # the bpk from.  fs_diff is destructive since it removes the .beep
    # sections when doing the second stage diff.  This prevents embedded
    # build info from triggering an install of that file.
    a_dir = os.path.join(tempdir, 'a')
    b_dir = os.path.join(tempdir, 'b')
    c_dir = os.path.join(tempdir, 'c')
    root_dir = os.path.join(tempdir, 'root')
    beep.update.uboot.extract_squashfs(args.image1, a_dir, True)
    beep.update.uboot.extract_squashfs(args.image2, b_dir, True)
    beep.update.uboot.extract_squashfs(args.image2, c_dir, True)
    # Create the new root to create the bpk out of.  We could do this out
    # of 'c' but it's easier for debugging.
    os.mkdir(root_dir)

    if args.ptest is True:
        print('Embedding partial_test files for end to end test')
        # Need to keep temp files for verification.
        args.save_temps = True
        # fs_diff will modify the only fs from image1.  Create an
        # extra copy to use for verification.
        base_dir = os.path.join(tempdir, 'base')
        beep.update.uboot.extract_squashfs(args.image1, base_dir, True)

        test_dir = os.path.realpath(os.path.join(os.path.split(__file__)[0],
                'assets/update/partial_test'))
        a_test = os.path.join(a_dir, 'test')
        b_test = os.path.join(b_dir, 'test')
        c_test = os.path.join(c_dir, 'test')
        base_test = os.path.join(base_dir, 'test')
        os.mkdir(a_test)
        os.mkdir(b_test)
        os.mkdir(c_test)
        os.mkdir(base_test)
        quick_cmd('cp -r {} {}'.format(os.path.join(test_dir, 'a', '*'),
                a_test))
        quick_cmd('cp -r {} {}'.format(os.path.join(test_dir, 'b', '*'),
                b_test))
        quick_cmd('cp -r {} {}'.format(os.path.join(test_dir, 'b', '*'),
                c_test))
        quick_cmd('cp -r {} {}'.format(os.path.join(test_dir, 'a', '*'),
                base_test))

    diff_dict = beep.update.partial.fs_diff(a_dir, b_dir)

    # Ignore files we don't care about or are protected.
    for dtype in diff_dict.keys():
        path_list = diff_dict[dtype]
        pattern_list = PARTIAL_IGNORE_RULES[dtype]
        for path in path_list[:]:
            match = [x for x in pattern_list if fnmatch.fnmatch(path, x)]
            if any(match):
                print('Ignoring \'{}\' in \'{}\' from rule \'{}\''.format(
                        path, dtype, match[0]))
                path_list.remove(path)

    # Copy the remaining files from c_dir to root_dir.
    # It is easiest to switch to c_dir so we can use --parents.
    cwd = os.getcwd()
    os.chdir(c_dir)

    for path in diff_dict['dif'] + diff_dict['add']:
        # Use path from root in the dict since we have switched to c_dir.
        # Use -P to not follow links.  The only time a directory will show
        # up in add or dif is if it is new or changed from a different file
        # type.  In both cases we want to recursively copy the new directory.
        quick_cmd('cp -rP --parents {} {}'.format(path, root_dir))
        if os.path.isdir(path):
            indir = quick_cmd('find {} -type f -or -type l'.format(path))
            if indir == '':
                print('**WARNING**: \'{}\' contains no files it will not be ' \
                        'created'.format(path))

    # Add files from roots on the command line.
    for add_root in (args.add_root or []):
        root_path = os.path.realpath(os.path.join(cwd, add_root))
        print('Adding files from root: {}'.format(add_root))
        os.chdir(root_path)
        quick_cmd('cp -rP --parents * {}'.format(root_dir))

    # Done with the copy.
    os.chdir(cwd)

    # Create bpk in root_dir and add all files.
    bpk = beep.update.bpk.Bpk(root_dir, args.key_file)
    bpk.add_files_all()

    # Add removes from the image diff.
    for path in diff_dict['del']:
        bpk.add_remove(path)

    # Add scripts specified from the command line.
    # format: [engine, path, when]
    pre = [x.split(':', 1) + ['pre'] for x in (args.prescript or [])]
    post = [x.split(':', 1) + ['post'] for x in (args.postscript or [])]

    for engine, path, when in pre + post:
        bpk.add_script(path, engine, when)

    # Add removes specified from the command line.
    for rpath in (args.remove or []):
        bpk.add_remove(rpath)

    # Last step is to sort the files and place /beep/platform/VERSION
    # as the last file to write.  If something bad happens the next time
    # beepupdate runs it will report the old version.
    bpk.sort_files()
    if bpk.has_file('beep/platform/VERSION'):
        # Move VERSION to the end of the manifest.
        bpk.del_file('beep/platform/VERSION')
        bpk.add_file('beep/platform/VERSION')
    else:
        print('**WARNING**: VERSION file does not exist in partial update.')
        print('**WARNING**: Make sure this is what you want.')

    print('\nbpk summary:')
    bpk.print_summary()

    # Don't re-sort, use abspath for output since it's done at the bpk root.
    man = bpk.gen_bpk(os.path.abspath(args.output), sort_files=False)

    # Cleanup.
    if args.save_temps is not True:
        shutil.rmtree(tempdir)
    else:
        print('Leaving temp directory: {}'.format(tempdir))

    print('Manifest for {}'.format(os.path.abspath(args.output)))
    print(str(man))

def main(argv):
    cmds = {
        'sign': sign_cmd,
        'del-sig': del_sig_cmd,
        'create-bpk': create_bpk_cmd,
        'partial-update': partial_update_cmd
    }

    parser = argparse.ArgumentParser(prog='bpktool',
            formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-d', '--debug', type=int, choices=(0,1,2))
    subparsers = parser.add_subparsers(help='sub-command help', dest='cmd',
            title='Commands', metavar='<command>')

    sign_parser = subparsers.add_parser('sign', help='sign file')
    sign_parser.add_argument('-o', '--output', type=str, dest='outpath',
            metavar='OUTPUT')
    sign_parser.add_argument('file_type', type=str,
            choices=sorted(tuple(STYLE_PREFIXES)))
    sign_parser.add_argument('method', type=str,
            choices=sorted(tuple(DIG_METHODS)))
    sign_parser.add_argument('inpath', type=str, metavar='input')
    sign_parser.add_argument('--key-file', nargs='+', type=str,
            dest='key_path', metavar='KEYFILE')

    del_sig_parser = subparsers.add_parser('del-sig',
            help='delete signature from file')
    del_sig_parser.add_argument('-o', '--output', type=str, dest='outpath',
            metavar='OUTPUT')
    del_sig_parser.add_argument('inpath', type=str, metavar='input')

    create_bpk_parser = subparsers.add_parser('create-bpk',
            help='create bpk file from the current directory',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog=textwrap.dedent('''\
            eg: bpktool.py create-bpk /path/to/key1 /path/to/key2 install.bpk
                    --prescript sh:scripta.sh lua:scriptb.lua
                    --postscript lua:scriptc.lua
            beepupdate will run scripta.sh, scriptb.lua, install remaining files,
                    then run scriptc.lua'''))
    create_bpk_parser.add_argument('key_file', type=str, nargs='+')
    create_bpk_parser.add_argument('output', type=str)
    create_bpk_parser.add_argument('--prescript', nargs='+', type=str,
            help='fmt: [lua|sh]:path of unsigned script to run before files ' \
            'are processed')
    create_bpk_parser.add_argument('--postscript', nargs='+', type=str,
            help='fmt: [lua|sh]:path of unsigned script to run after files ' \
            'are processed')
    create_bpk_parser.add_argument('--remove', nargs='+', type=str,
            help='paths to be removed during update')

    partial_update_parser = subparsers.add_parser('partial-update',
            help='create partial bpk from two OpenWrt images')
    partial_update_parser.add_argument('image1', type=str,
            help='image to start the diff from')
    partial_update_parser.add_argument('image2', type=str,
            help='image to diff to')
    partial_update_parser.add_argument('key_file', type=str, nargs='+')
    partial_update_parser.add_argument('output', type=str,
            help='bpk output')
    partial_update_parser.add_argument('--prescript', nargs='+', type=str,
            help='fmt: [lua|sh]:path of unsigned script to run before files ' \
            'are processed')
    partial_update_parser.add_argument('--postscript', nargs='+', type=str,
            help='fmt: [lua|sh]:path of unsigned script to run after files ' \
            'are processed')
    partial_update_parser.add_argument('--remove', nargs='+', type=str,
            help='additional paths to be removed during update')
    partial_update_parser.add_argument('--add-root', nargs='+', type=str,
            help='additional roots to add during update')
    partial_update_parser.add_argument('--save-temps', action='store_true',
            help='do not remove temp directory')
    partial_update_parser.add_argument('--ptest', action='store_true',
            help='embed partial_test (DEBUG ONLY)')

    if 'argcomplete' in sys.modules.keys():
        argcomplete.autocomplete(parser)

    args = parser.parse_args()

    global debug
    debug = args.debug
    dprint(args)

    cmds[args.cmd](args)

if __name__ == '__main__':
    main(sys.argv)
