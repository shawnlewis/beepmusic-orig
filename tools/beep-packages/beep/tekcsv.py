class Tekcsv(object):
    def __init__(self, path):
        self.path = path
        self.file = open(path, 'r')
        self.channels = 0
        self._read_ch_info()
        self.cur_idx = 0
        self.cur_data = None

    def _add_ch_info(self, d):
        ch_key = d[0::6]
        ch_val = d[1::6]
        if len([x for x in ch_key if x != '']) == 0 or \
                len([x for x in ch_val if x != '']) == 0:
            return 0
        for x in xrange(self.channels):
            if ch_key[x] == '' or ch_val[x] == '':
                continue
            self.ch_info[x][ch_key[x]] = ch_val[x]
        return 1

    def _read_ch_info(self):
        pos = self.file.tell()
        self.file.seek(0)

        d = self.file.readline().split(',')
        if len(d) == 0 or (len(d) % 6) != 0:
            raise Exception('invalid meta data')
        self.channels = len(d) / 6
        self.ch_info = [dict() for x in xrange(self.channels)]

        meta_count = self._add_ch_info(d)
        lines_left = 19
        for line in self.file:
            d = line.split(',')
            meta_count += self._add_ch_info(d)
            lines_left -= 1
            if lines_left == 0:
                break
        if meta_count == 0:
            raise Exception('no meta data found')

        self.file.seek(pos)

        for same_k in ['Record Length', 'Sample Interval']:
            if not self._ch_info_same(same_k):
                print('Warning \'{}\' is different between channels'.format(
                        same_k))

        self.allow_delta = float(self.ch_info[0]['Sample Interval']) * 10

    def _get_ch_info(self, k, d=None):
        return [x.get(k, d) for x in self.ch_info]

    def _ch_info_same(self, k):
        info = self._get_ch_info(k, None)
        if len(set(info)) != 1 or info[0] is None:
            return False
        return True

    def __iter__(self):
        return self

    def next(self):
        d = self.next_point()
        if d is None:
            raise StopIteration
        return d

    def channel_names(self):
        return self._get_ch_info('Source', 'unknown')

    def next_point(self):
        while 1:
            self.cur_pos = self.file.tell()
            self.cur_idx += 1
            d = self.file.readline()

            # EOF.
            if d == '':
                self.cur_data = None
                self.cur_idx = -1
                return self.cur_data

            d = d.split(',')
            d = [d[3]] + d[4::6]
            d = [float(x) for x in d]
            d += [0.] * (self.channels + 1 - len(d))
            if self.cur_data is not None and \
                    (d[0] > (self.cur_data[0] + self.allow_delta) or \
                    (d[0] < self.cur_data[0])):
                print('dropping bad point {}'.format(self.cur_idx))
                #print('d[0]: {} self.cur_data[0]: {} self.allow_delta: {}'.format(
                #        d[0], self.cur_data[0], self.allow_delta))
                continue

            self.cur_data = d
            return self.cur_data
