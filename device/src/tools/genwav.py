import struct
import sys
import wave

def create_wave():
    freq = 440.0
    data_size = 40000
    fname = "WaveTest.wav"
    frate = 44100.0  # framerate as a float
    amp = 64000.0     # multiplier for amplitude

    num_periods = 10000

    samples = []
    for x in xrange(num_periods):
        for y in xrange(33):
            samples.append(0)
        for y in xrange(33):
            samples.append(0x55555555)

    nframes = num_periods * 4 / 3 / 2.0
    print 'number frames', nframes

    wav_file = wave.open(fname, "w")

    nchannels = 2
    sampwidth = 3
    framerate = int(frate)
    comptype = "NONE"
    compname = "not compressed"

    wav_file.setparams((nchannels, sampwidth, framerate, nframes,
        comptype, compname))

    for s in samples:
        # write the audio frames to file
        wav_file.writeframes(struct.pack('L', s))

    wav_file.close()


def main(argv):
    create_wave()

if __name__ == '__main__':
    main(sys.argv)
