import copy
import os

from beep.utils import quick_cmd

# [<record name>, <record len>]
RECORD_INFO = {
    'ma': ['malloc',            5],
    'ca': ['calloc',            6],
    're': ['realloc',           6],
    'fr': ['free',              4],
    'me': ['memalign',          6],
    'va': ['valloc',            5],
    'po': ['posix_memalign',    7],
    'cf': ['cfree',             4],
    'pv': ['pvalloc',           5],
}

def record_id(record):
    return record.split(' ')[0]

def record_code(record):
    code = record.split(' ')[1]
    if code not in RECORD_INFO.keys():
        raise Exception('unknown code %s' % code)
    return code

def record_type_from_code(code):
    return RECORD_INFO[code][0]

def record_type(record):
    return RECORD_INFO[record_code(record)][0]

def record_len(record):
    return RECORD_INFO[record_code(record)][1]

def record_check(record):
    if not isinstance(record, str):
        return False
    return len(record.split(' ')) == record_len(record)

def str_to_ptr(string):
    if string == '(nil)':
        return 0
    return int(string, 16)

# Returns (removed pointer, added pointer), pointer may be None.
def record_ptrs(record):
    code = record_code(record)  # Check valid code.
    d = record.split(' ')
    if code == 'fr' or code == 'cf':
        return (str_to_ptr(d[-1]), None)
    elif code == 're':
        return (str_to_ptr(d[3]), str_to_ptr(d[-1]))
    elif code == 'po':
        return (None, str_to_ptr(d[3]))
    else:
        return (None, str_to_ptr(d[-1]))

# Size in bytes of record. None is unknown as malloc(0) is valid.
# Returns size even if returned pointer is NULL.
def record_size(record):
    code = record_code(record)  # Check valid code.
    d = record.split(' ')
    if code == 'ma':
        return int(d[3], 16)
    elif code == 'ca':
        return int(d[3], 16) * int(d[4], 16)
    elif code == 're':
        return int(d[4], 16)
    elif code == 'me':
        return int(d[4], 16)
    elif code == 'va':
        return int(d[3], 16)
    elif code == 'po':
        return int(d[5], 16)
    elif code == 'pv':
        return int(d[3], 16)
    else:
        return None

def record_caller(record):
    return record.split(' ')[2]

def record_caller_addr(record):
    return record.split(' ')[2].split('[')[1][:-1]

def record_caller_file(record):
    return record.split(' ')[2].split('[')[0]

def _record_caller_line_caller(caller, root_dir, cwd=None):
    a = caller.split('[')[1][:-1]
    f = caller.split('[')[0]
    if f[0] == '.' and cwd != None:
        f = os.path.join(cwd, f)
    if f[0] == '/':
        f = f[1:]
    f = os.path.join(root_dir, f)
    l = quick_cmd('addr2line -e {} {}'.format(f, a))
    return l

def record_caller_line(record, root_dir, cwd=None):
    return _record_caller_line_caller(record_caller(record), root_dir, cwd)

class NMTrace(object):
    TRANSACTION_VALID =                 0
    # Examples of nil: malloc(0), free(NULL), first part of realloc(NULL, ...).
    TRANSACTION_NIL =                   1
    TRANSACTION_FREE_BEFORE_ALLOC =     2
    TRANSACTION_DOUBLE_FREE =           3
    # Probably a missed record or error in this code
    TRANSACTION_DOUBLE_ALLOC =          4
    TRANSACTION_FAILED_ALLOC =          5
    TRANSACTION_STR = ('valid',
            'nil',
            'free before alloc',
            'double free',
            'double alloc',
            'failed alloc')

    def __init__(self, path=None, caller_root_dir='/', caller_trim_dir=None,
            trace_cwd=None):
        self.path = None
        self.file = None
        self.cur_record = None
        self.cur_transaction = None
        self.cur_lineno = None
        self.cur_pos = None
        self.max_lineno = None
        self.max_pos = None
        self.max_valid = None
        self.err_at_max = None
        self.addr_range = None

        self.mmap = None
        self.caller_map = None

        self.caller_root_dir = caller_root_dir
        self.caller_trim_dir = caller_trim_dir
        self.trace_cwd = trace_cwd

        if path is not None:
            self.open(path)

    def __len__(self):
        ret, _, _ = self.get_stats()
        return ret

    def __iter__(self):
        return self

    def next(self):
        line = self.record_next()
        if line == None:
            raise StopIteration
        return line

    def _reset_state(self):
        self.cur_record = None
        self.cur_transaction  = None
        self.cur_lineno = 0
        self.cur_pos = 0
        self.max_lineno = 0
        self.max_pos = 0
        self.max_valid = False
        self.err_at_max = 0
        self.addr_range = [(2**64 - 1),0]
        self.mmap = {}
        self.caller_map = {}

    # Getting the file:line from addr2line is expensive so cache the
    # results.
    def _resolve_caller(self, record=None, caller=None):
        if record is None and caller is None:
            raise Exception('must specify record or caller')
        if caller is None:
            caller = record_caller(record)
        nice_caller = self.caller_map.get(caller, None)
        if nice_caller is not None:
            return nice_caller

        try:
            nice_caller = _record_caller_line_caller(caller,
                    self.caller_root_dir, self.trace_cwd)
            if nice_caller.startswith(self.caller_trim_dir):
                nice_caller = nice_caller[len(self.caller_trim_dir):]
        except:
            nice_caller = caller

        self.caller_map[caller] = nice_caller
        return nice_caller

    def _update_transaction(self):
        record = self.cur_record
        free_ptr, alloc_ptr = record_ptrs(record)
        rsize = record_size(record)
        rcode = record_code(record)

        # tra == [free tra, alloc tra]
        # each tra == (tra code, record code, ptr, size)
        tra = [None, None]
        if free_ptr is not None:
            if free_ptr == 0:
                tra[0] = (self.TRANSACTION_NIL, rcode, free_ptr, 0)
            else:
                mentry = self.mmap.get(free_ptr, None)
                if mentry is None:
                    tra[0] = (self.TRANSACTION_FREE_BEFORE_ALLOC,
                            rcode, free_ptr, 0)
                elif mentry[0] == True:
                    tra[0] = (self.TRANSACTION_VALID,
                            rcode, free_ptr, mentry[1])
                else:  # mentry[0] == False.
                    tra[0] = (self.TRANSACTION_DOUBLE_FREE,
                            rcode, free_ptr, 0)

        if alloc_ptr is not None:
            if alloc_ptr == 0:
                tra[1] = (self.TRANSACTION_FAILED_ALLOC,
                        rcode, 0, 0)
            else:
                mentry = self.mmap.get(alloc_ptr, None)
                # Cases:
                #   ptr never alloced.
                #   ptr was freed.
                #   realloc returning same pointer with different size.
                if mentry is None or mentry[0] == False or \
                        (mentry[0] == True \
                        and free_ptr == alloc_ptr \
                        and rcode == 're' \
                        and tra[0][0] == self.TRANSACTION_VALID):
                    tra[1] = (self.TRANSACTION_VALID, rcode, alloc_ptr, rsize)
                else:  # mentry[0] == True.
                    tra[1] = (self.TRANSACTION_DOUBLE_ALLOC,
                            rcode, alloc_ptr, 0)

        self.cur_transaction = tra

    def _apply_transaction(self):
        tra = self.cur_transaction
        if tra is None:
            return

        caller = record_caller(self.cur_record)

        # tra == [free tra, alloc tra]
        # each tra == (tra code, record code, ptr, size)
        # mentry = self.mmap[ptr]
        # mentry == (currently alloced (bool), size, record code, caller)
        # Again free before alloc.
        if tra[0] is not None and tra[0][0] == self.TRANSACTION_VALID:
            self.mmap[tra[0][2]] = (False, tra[0][3], tra[0][1], caller)

        if tra[1] is not None and tra[1][0] == self.TRANSACTION_VALID:
            self.mmap[tra[1][2]] = (True, tra[1][3], tra[1][1], caller)
            # Update stats only if this is a new line.
            # Note: walking the data may give a more accurate range as the
            # values will have been verified as successful in
            # _update_transaction then iterating the trace file quickly in
            # get_stats.  This should be fine as we're only going to use
            # this for making visual representations of the heap.
            if self.cur_lineno == self.max_lineno:
                self.addr_range[0] = min(self.addr_range[0],
                        tra[1][2])
                self.addr_range[1] = max(self.addr_range[1],
                        tra[1][2] + tra[1][3])

    def open(self, path):
        self.close()
        self.file = open(path, 'r')
        self.path = path
        self._reset_state()

    def close(self):
        if self.file:
            self.file.close()
            self.file = None
        self.path = None
        self._reset_state()

    def get_stats(self):
        if self.max_valid is not True:
            while True:
                pos = self.file.tell()
                rec = self.file.readline().strip()
                if rec != '':
                    # Keeping inline with record_next, update the lineno and
                    # pos for every line valid or not.
                    self.max_pos = pos
                    self.max_lineno += 1
                    # Only update stats for valid records.
                    if record_check(rec):
                        rsize = record_size(rec)
                        _, alloc_ptr = record_ptrs(rec)
                        if rsize is not None and alloc_ptr is not None:
                            self.addr_range[0] = min(self.addr_range[0],
                                    alloc_ptr)
                            self.addr_range[1] = max(self.addr_range[1],
                                    alloc_ptr + rsize)
                else:
                    self.max_valid = True
                    self.file.seek(self.cur_pos)
                    if self.cur_lineno:
                        # cur_pos is taken before cur_line, advance the file
                        # pos to be at the next line, but only if a record
                        # has not been read yet.
                        self.file.readline()
                    break
        return self.max_lineno, self.addr_range[0], self.addr_range[1]

    def get_filtered_stats(self, ignore_below=0, ignore_above=(2**64 - 1)):
        self.file.seek(0)
        # This will only track alloc_ptr count so the sum will be the
        # same as get_stats()[0].
        lineno = 0
        addr_range = [(2**64 - 1), 0]

        for rec in self.file:
            rec = rec.strip()
            if record_check(rec):
                rsize = record_size(rec)
                _, alloc_ptr = record_ptrs(rec)
                if rsize is not None and alloc_ptr is not None:
                    add_line = 0
                    if alloc_ptr >= ignore_below and alloc_ptr < ignore_above:
                        addr_range[0] = min(addr_range[0], alloc_ptr)
                        addr_range[1] = max(addr_range[1], alloc_ptr + rsize)
                        lineno += 1

        self.file.seek(self.cur_pos)
        if self.cur_lineno:
            self.file.readline()

        return lineno, addr_range[0], addr_range[1]

    def get_mmap(self, make_copy=True):
        if make_copy is True:
            return copy.deepcopy(self.mmap)
        else:
            return self.mmap

    def get_annotated_mmap(self, include_free):
        if include_free is True:
            mmap = self.get_mmap(True)
        else:
            mmap = {k:v for k,v in self.mmap.iteritems() if v[0] == True}

        for k in mmap.keys():
            v = mmap[k]
            mmap[k] = (v[0], v[1], v[2], self._resolve_caller(caller=v[3]))
        return mmap

    def record_next(self):
        self.cur_pos = self.file.tell()
        self.cur_lineno += 1
        self.cur_record = self.file.readline()

        # EOF
        if self.cur_record == '':
            self.cur_record = None
            self.cur_transaction = None
            self.cur_lineno = -1
            return self.cur_record

        self.cur_record = self.cur_record.strip()
        if self.cur_lineno > self.max_lineno:
            self.max_lineno = self.cur_lineno
            self.max_pos = self.cur_pos

        if record_check(self.cur_record):
            self._update_transaction()
            self._apply_transaction()
        else :
            if self.max_lineno == self.cur_lineno:
                self.err_at_max += 1
            # Invalid line.
            self.cur_record = ''
            self.cur_transaction = None
        return self.cur_record

    def record_id(self, record=None):
        try:
            return record_id(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_code(self, record=None):
        try:
            return record_code(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_type(self, record=None):
        try:
            return record_type(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_len(self, record=None):
        try:
            return record_len(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_check(self, record=None):
        try:
            return record_check(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_ptrs(self, record=None):
        try:
            return record_ptrs(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_size(self, record=None):
        try:
            return record_size(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_caller(self, record=None):
        try:
            return record_caller(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_caller_addr(self, record=None):
        try:
            return record_addr(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_caller_file(self, record=None):
        try:
            return record_file(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    def record_caller_line(self, record=None):
        try:
            return record_line(record or self.cur_record)
        except:
            if self.cur_record is not None:
                return 'Error at line {}'.format(self.line_no)
            else:
                return None

    # Requires internal state so don't use optional record argument.
    def record_annotate(self):
        rec = self.cur_record
        tra = self.cur_transaction
        if rec is None or rec == '' or tra is None or tra == [None, None]:
            return None

        ret = []
        nice_caller = self._resolve_caller(record=rec)
        rtype = record_type(rec)

        # tra == [free tra, alloc tra]
        # each tra == (tra code, record code, ptr, size)
        # Again free before alloc.
        if tra[0] is not None:
            if tra[0][0] == self.TRANSACTION_VALID:
                ret.append('-: {} bytes at 0x{:x} from {} at {}'.format(
                        tra[0][3], tra[0][2], rtype, nice_caller))
            elif tra[0][0] != self.TRANSACTION_NIL:
                err_str = self.TRANSACTION_STR[tra[0][0]]
                ret.append('**{}** for 0x{:x} from {} at {}'.format(
                        err_str, tra[0][2], rtype, nice_caller))

        if tra[1] is not None:
            if tra[1][0] == self.TRANSACTION_VALID:
                ret.append('+: {} bytes at 0x{:x} from {} at {}'.format(
                        tra[1][3], tra[1][2], rtype, nice_caller))
            elif tra[1][0] != self.TRANSACTION_NIL:
                err_str = self.TRANSACTION_STR[tra[1][0]]
                ret.append('**{}** for 0x{:x} from {} at {}'.format(
                        err_str, tra[1][2], rtype, nice_caller))

        if len(ret) == 0:
            return None
        return '\n'.join(ret)
