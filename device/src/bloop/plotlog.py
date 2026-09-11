import matplotlib.pyplot as plt
import sys

def main(argv):
    bloop_log = open(argv[1])
    lines = [l.split() for l in bloop_log.readlines()]

    lines = [l for l in lines if '70:8d:0b' in l[3]]

    times = [float(l[0]) for l in lines]
    sizes = [float(l[2]) for l in lines]

    plt.plot(times, sizes, '.')
    plt.show()

if __name__ == '__main__':
    main(sys.argv)
