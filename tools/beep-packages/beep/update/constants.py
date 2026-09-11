# From beepupdate/verify.c
SIGNED_MAGIC =                  '\xb5\xb9\xde\xbc'

DIG_METHODS = {
    'md5':                      '0',
    'sha1':                     '1',
    'sha256':                   '2',
    'sha512':                   '3',
    'signed-sha256':            '4',
    'signed-sha512':            '5'
}

DIG_CODES = {
    '0':                        'md5',
    '1':                        'sha1',
    '2':                        'sha256',
    '3':                        'sha512',
    '4':                        'signed-sha256',
    '5':                        'signed-sha512'
}

# Size if the digest is written out as a hex string.
# -1 indicates a variable size.
DIG_SIZE = {
    'md5':                      32,
    'sha1':                     40,
    'sha256':                   64,
    'sha512':                   128,
    'signed-sha256':            -1,
    'signed-sha512':            -1,
}

DIG_STYLE = [
    'bin',
    'lua',
    'manifest',
    'sh',
    'yml'
]

# Format: (prefix, postfix, is_str)
STYLE_INFO = {
    'bin':                      ('', '', False),
    'lua':                      ('-- ', '\n', True),
    'sh':                       ('# ', '\n', True),
    'yml':                      ('# ', '\n', True)
}

# TODO: See if this can be removed later.
STYLE_PREFIXES = {
    'bin':                      None,
    'lua':                      '-- ',
    'sh':                       '# ',
    'yml':                      '# '
}

MANIFEST_FILE_TYPES = [
    'file',
    'rm',
    'sh',
    'lua',
    #'rfirm',
    #'pfirm'
]

REMOVE_FLAGS = {
    'CONT_ON_ERR':              0x00000001,
    'ALLOW_DIRS':               0x00000002,
    'ALLOW_RESURSIVE':          0x00000004,
    'INVALID':                  0xfffffff8
}
