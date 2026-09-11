#!/usr/bin/python

import collections
import copy
import itertools
import subprocess
import sys


# packet fields
TS = 0
NUM = 1
LEN = 2
SOURCE = 3
DEST = 4


def wifi_data_packets():
    cmd = './monitor'
    log = open('blooplog.txt', 'w')
    while True:
        proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
        while True:
            line = proc.stdout.readline()
            log.write(line)

            # TODO: Why does this happen sometimes?
            if not line:
                break

            fields = line.split()
            try:
                fields = (float(fields[TS]),
                          int(fields[NUM]),
                          int(fields[LEN]),
                          fields[SOURCE],
                          fields[DEST])
                yield fields
            except IndexError:
                print './monitor error: ', line
                continue

# dict of ((src, dest), [chains])
#    chains is dict of {seq: chain} pairs
#    seq is (len0, len1, len2, ...)
#    chain is [packet0, packet1, packet2, ...]

def extends_chain(chain, packet):
    if packet[0] < chain[0][0]:
        return False
    if chain[-1][0] + .5 < packet[0]:
        return False
    if len(chain) == 1:
        if packet[2] > chain[0][2]:
            return True
        else:
            return False
    len_delta = chain[-1][2] - chain[-2][2]
    if chain[-1][2] + len_delta == packet[2]:
        return True
    return False

def print_stats(pairs):
    print 'Num pairs:', len(pairs)
    chain_counts = sorted([(len(c), p) for p, c in pairs.iteritems()])
    print chain_counts[-10:]

    #if chain_counts:
    #    high_pair = chain_counts[-1][1]
    #    print 'HIGH PAIR'
    #    print pairs[high_pair]
    sys.stdout.flush()


logs = {}
def process_packets(process_fn):
    global logs
    count = 0
    pairs = {}
    for packet in wifi_data_packets():
        ts, num, length, source, dest = packet
        #if (count % 100 == 0):
        #    print 'COUNT:', count
        #    print_stats(pairs)
        count += 1
        #print count

        pair = (source, dest)
        if pair not in pairs:
            pairs[pair] = {}
            logs[pair] = collections.deque()

        logs[pair].append(packet)

        # keep 2 seconds of log
        while logs[pair] and packet[TS] - logs[pair][0][TS] > 10.0:
            logs[pair].popleft()

        pairs[pair] = process_fn(pairs[pair], logs[pair], packet)

def print_logs():
    for pair, log in logs.iteritems():
        print pair
        for packet in log:
            print packet

import atexit
atexit.register(print_logs)



def chain_search(filter_fn, extend_fn, update_fn, is_done_fn, handle_done_fn,
                 update_done_fn):
    def process_fn(chains, log, packet):
        chains = filter_fn(chains, packet)
        new_chains = extend_fn(chains, packet)
        chains = update_fn(chains, new_chains)
        for chain in new_chains:
            if is_done_fn(chain):
                handle_done_fn(chain, log)
                update_done_fn(chains, chain)
        return chains
    return process_fn


def filter_chains(keep_fn):
    def filter(chains, packet):
        discard_keys = []
        for seq, chain in chains.iteritems():
            if not keep_fn(chain, packet):
                discard_keys.append(seq)
        for key in discard_keys:
            chains.pop(key)
        return chains
    return filter

def keep_time_delta(delta):
    def keep_fn(chain, packet):
        if packet[TS] - chain[0][TS] > delta:
            return False
        return True
    return keep_fn

def extend_chains(extends_fn):
    def extend(chains, packet):
        new_chains = [[packet]]
        for chain in chains.itervalues():
            if extends_fn(chain, packet):
                new_chain = copy.copy(chain)
                new_chain.append(packet)
                new_chains.append(new_chain)
        return new_chains
    return extend

def replace_seqs(chains, new_chains):
    for chain in new_chains:
        seq = tuple(packet[LEN] for packet in chain)
        chains[seq] = chain
    return chains

def chain_complete(chain):
    if len(chain) == 5:
        return True
    return False

BASE = 182
total_bloops = 0
signal = []
def handle_done(chain, packet_log):
    if chain[0][LEN] != BASE + 10: # and chain[0][LEN] != BASE + 20:
        return

    global total_bloops
    print 'FOUND BLOOP ', total_bloops, ' source:', chain[0][3], 'dest:', chain[0][4]
    total_bloops += 1

    levels = {}
    for packet in packet_log:
        if packet[NUM] < chain[0][NUM]:
            continue
        if packet[NUM] > chain[-1][NUM]:
            continue

        if (packet[LEN] - BASE) % 11 == 0:
            level = (packet[LEN] - BASE) / 11
            if level not in levels:
                levels[level] = 0
            levels[level] += 1

    vals = []
    if 6 in levels and 7 in levels and 8 in levels:
        vals.append(0)
    if 9 in levels and 10 in levels and 11 in levels:
        vals.append(1)

    if 12 in levels and 13 in levels and 14 in levels:
        vals.append(2)
    if len(vals) == 1:
        signal.append(vals[0])
        #print packet
    print len(signal), signal

    # find start and end of chain in packet_log
    # search chain for 0, 1, 2
    # append result to signal

    # try to process signal and find a word

def delete_combinations(chains, chain):
    seq = tuple(packet[LEN] for packet in chain)

    for i in xrange(1, len(chain) + 1):
        for combo in itertools.combinations(seq, i):
            try:
                #print combo
                chains.pop(combo)
            except KeyError:
                pass
                #print combo, 'Not found'

    return chains
    # TODO: delete permutations of seq as well.


def main(argv):
    process_packets(chain_search(filter_chains(keep_time_delta(1.0)),
                                 extend_chains(extends_chain),
                                 replace_seqs,
                                 chain_complete,
                                 handle_done,
                                 delete_combinations))


if __name__ == '__main__':
    main(sys.argv)
