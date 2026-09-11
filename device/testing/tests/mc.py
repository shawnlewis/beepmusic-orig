"""Lightweight Markov Chain implementation"""

import random, os

class State(object):
    """Markov Chain state node"""
    def __init__(self, ttable=None):
        if ttable:
            self.set_ttable(ttable)
        else:
            self.ttable = None

    def set_ttable(self, ttable):
        states = zip(*ttable)[0]

        # Convert to cumulative probabilities
        probs = zip(*ttable)[1]
        cprobs = [sum(probs[:n+1]) for n in xrange(len(probs))]
        denom = float(cprobs[-1])
        norm_cprobs = [x/denom for x in cprobs]
        assert norm_cprobs[-1] == 1, 'Last cumulative probability != 1.0'

        self._ttable = zip(states, norm_cprobs)

    def get_transition(self, x):
        assert x >= 0 and x <= 1, 'x must be 0 <= x <= 1'

        if not hasattr(self, '_ttable'):
            return self

        for t in self._ttable:
            if t[1] >= x:
                if not t[0]:
                    return self
                else:
                    return t[0]

class RunnableState(State):
    def __init__(self, run, ttable=None):
        super(RunnableState, self).__init__(ttable)
        self.run = run

class MarkovChain(object):
    def __init__(self,state,seed=None):
        self.state = state

        if seed:
            self.seed = seed
        else:
            # Use cryptographic random to generate seed
            self.seed = int(os.urandom(4).encode('hex'),16)

        # DON'T use module-level methods -- keep random state separate from
        # tests so we can reproduce state transitions
        self.random = random.Random(self.seed)
        self.steps = 0

    def step(self):
        x = random.random()
        new_state = self.state.get_transition(x)
        self.state = new_state
        self.steps += 1
        return self.state

    def dump(self):
        return self.seed, self.steps

if __name__ == '__main__':
    import StringIO, sys

    print 'Running self-test'

    output = StringIO.StringIO()

    def make_printer(msg):
        def printer():
            output.write(msg)
        return printer

    A = RunnableState(make_printer('A'))
    B = RunnableState(make_printer('B'))
    C = RunnableState(make_printer('C'))

    A.set_ttable([(None,3),(B,1)])
    B.set_ttable([(None,1),(C,1)])
    C.set_ttable([(None,0.1),(A,0.9)])

    mc = MarkovChain(A)
    for _ in xrange(1000000):
        if _ % 25000 == 0:
            sys.stderr.write('.')
        mc.state.run()
        mc.step()

    sys.stderr.write('\n')

    outstr = output.getvalue()
    output.close()

    def count(sub):
        count = start = 0
        while True:
            start = outstr.find(sub, start) + 1
            if start > 0:
                count += 1
            else:
                return count

    def pattern_ratio(x,y):
        return count(x) / float(count(y))

    def pequal(x,y,fuzz=0.05):
        "Returns true if x and y are practically equivalent given fuzz"
        MOPE = ((x+y)/2) * fuzz # Margin Of Practicel Equivalence
        return abs(x-y) <= MOPE

    try:
        assert pequal(pattern_ratio('AA','AB'),3.0), \
                'Ratio of AA and AB transitions is unlikely'

        assert pequal(pattern_ratio('BB','BC'),1.0), \
                'Ratio of BB and BC transitions is unlikely'

        assert pequal(pattern_ratio('CA','CC'),9.0), \
                'Ratio of CA and CC transitions is unlikely'
    except:
        sys.stderr.write('Seed: %d Steps: %d\n' % mc.dump())
        raise

    print 'Done.'
