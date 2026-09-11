import commands
import glob
import os
import sys
import tempfile

TEMPDIR_PREFIX = 'tmps3'
ARCHIVE_BUCKET = 'ab54e530b2'

# Public key encrypted files:
# output.tar.gz.pub
#  - key1.bin.enc       # First 128 bits of 256 bit key (encrypted with public key).
#  - key2.bin.enc       # Second 128 bits of 256 bit key (encrypted with public key).
#  - sha512.txt.enc     # sha512 of unencrypted data.tar.gz (encrypted with key1 + key2).
#  - data.enc           # Data (encrypted with key1 + key2).

def quick_cmd(cmd):
    s, o = commands.getstatusoutput(cmd)
    if (s != 0):
        raise Exception('Error: \'{}\' exited with code: {}'.format(cmd, s))
    return o


def create_tempdir(temp_in_home):
    # Do not use /tmp by default as this could use swap which the servers do
    # not have.
    if temp_in_home is True:
        temproot = os.path.expanduser('~')
    else:
        temproot = '/tmp'

    return tempfile.mkdtemp(prefix=TEMPDIR_PREFIX, dir=temproot)


def clean_tempdir(path):
    if not os.path.isdir(path):
        raise Exception('Error: path \'{}\' must be a directory'.format(
                path))

    dirname = os.path.split(path)[1]

    if not dirname.startswith(TEMPDIR_PREFIX):
        raise Exception('Error: path \'{}\' does not start with \'{}\''.format(
                dirname, TEMPDIR_PREFIX))

    quick_cmd('rm -rf {}'.format(path))


def s3encrypt(pub_key_path, plaintext_path, public_path, temp_in_home=True):
    if not os.path.isfile(pub_key_path):
        raise Exception('Error: pub_key_path \'{}\' must be a file'.format(
                pub_key_path))

    if not os.path.isfile(plaintext_path):
        raise Exception('Error: plaintext_path \'{}\' must be a file'.format(
                plaintext_path))

    tempdir = create_tempdir(temp_in_home)

    # There is not a good openssl python wrapper so just use the cmd line.
    quick_cmd('openssl rand 128 > {}/key1.bin'.format(tempdir))
    quick_cmd('openssl rand 128 > {}/key2.bin'.format(tempdir))
    quick_cmd('cat {}/key1.bin {}/key2.bin >> {}/key.bin'.format(tempdir,
            tempdir, tempdir))

    # Encrypt the keys using the public key.
    quick_cmd('openssl rsautl -encrypt -inkey {} -pubin -in {}/key1.bin ' \
            '-out {}/key1.bin.enc'.format(
            pub_key_path, tempdir, tempdir))
    quick_cmd('openssl rsautl -encrypt -inkey {} -pubin -in {}/key2.bin ' \
            '-out {}/key2.bin.enc'.format(
            pub_key_path, tempdir, tempdir))

    # Hash the plaintext data file.
    quick_cmd('openssl sha512 -hex {} | sed \'s/^.* //\' > ' \
            '{}/sha512.txt'.format(
            plaintext_path, tempdir))
    # Encrypt the hash.
    quick_cmd('openssl enc -aes-256-cbc -salt -in {}/sha512.txt -out ' \
            '{}/sha512.txt.enc -pass file:{}/key.bin'.format(
            tempdir, tempdir, tempdir))

    # Encrypt the data file.
    pt_filename = os.path.split(plaintext_path)[1]
    quick_cmd('openssl enc -aes-256-cbc -salt -in {} -out {}/{}.enc -pass ' \
            'file:{}/key.bin'.format(
            plaintext_path, tempdir, pt_filename, tempdir))

    # Tar up all of the encrypted files.
    public_path += '.pub.tar.gz'
    quick_cmd('tar -C {} -c -z -v -f {} key1.bin.enc key2.bin.enc ' \
            'sha512.txt.enc {}.enc'.format(
            tempdir, public_path, pt_filename))

    clean_tempdir(tempdir)

    return public_path

def s3decrypt(priv_key_path, public_path, plaintext_dir, temp_in_home=True):
    if not os.path.isfile(priv_key_path):
        raise Exception('Error: priv_key_path \'{}\' must be a file'.format(
                priv_key_path))

    if not os.path.isfile(public_path):
        raise Exception('Error: public_path \'{}\' must be a file'.format(
                public_path))

    if not os.path.isdir(plaintext_dir):
        raise Exception('Error: plaintext_dir \'{}\' must be a directory'.format(
                plaintext_dir))

    tempdir = create_tempdir(temp_in_home)

    # Extract the encrypted files.
    quick_cmd('tar -C {} -x -z -v -f {}'.format(
            tempdir, public_path))

    # Decrypt the keys using the private key.
    quick_cmd('openssl rsautl -decrypt -inkey {} -in {}/key1.bin.enc ' \
            '-out {}/key1.bin'.format(
            priv_key_path, tempdir, tempdir))
    quick_cmd('openssl rsautl -decrypt -inkey {} -in {}/key2.bin.enc ' \
            '-out {}/key2.bin'.format(
            priv_key_path, tempdir, tempdir))

    # Cat the two key files together.
    quick_cmd('cat {}/key1.bin {}/key2.bin >> {}/key.bin'.format(tempdir,
            tempdir, tempdir))

    # Decrypt the hash.
    quick_cmd('openssl enc -d -aes-256-cbc -in {}/sha512.txt.enc -out ' \
            '{}/sha512.txt -pass file:{}/key.bin'.format(
            tempdir, tempdir, tempdir))

    # Find the encrypted data file.
    known_files = ['sha512.txt.enc', 'key2.bin.enc', 'key1.bin.enc']
    enc_files = glob.glob('{}/*.enc'.format(tempdir))
    ct_path = [x for x in enc_files if os.path.split(x)[1] not in known_files]
    if len(ct_path) != 1:
        raise Exception('Could not find data file')

    # Decrypt the data file.
    ct_path = os.path.split(ct_path[0])[1]
    pt_path = os.path.join(plaintext_dir, ct_path[:-4])
    quick_cmd('openssl enc -d -aes-256-cbc -in {}/{} -out {} -pass ' \
            'file:{}/key.bin'.format(
            tempdir, ct_path, pt_path, tempdir))

    # Verify the hash of the plaintext file.
    hash_file = open('{}/sha512.txt'.format(tempdir))
    # Strip the trailing new line.
    known_hash = hash_file.read().strip()
    hash_file.close()

    new_hash = quick_cmd('openssl sha512 -hex {} | sed \'s/^.* //\''.format(
            pt_path))

    if new_hash != known_hash:
        raise Exception('Plaintext file hashes do not match')

    clean_tempdir(tempdir)


def s3archive(config, path, archive_dir):
    if not os.path.isfile(config):
        raise Exception('config file \'{}\' not found'.format(config))

    if not os.path.isfile(path):
        raise Exception('upload file \'{}\' not found'.format(path))

    if not archive_dir.endswith('/'):
        archive_dir += '/'

    quick_cmd('s3cmd -c {} put {} s3://{}/archive/{}'.format(config, path,
            ARCHIVE_BUCKET, archive_dir))
