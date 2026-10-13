"""mux.py OUT.mp4 SOUND.wav WIDTH HEIGHT FPS < frames.rgb

Makes an MP4 (H.264 and AAC) of the raw RGB24 pictures tools/swvideo writes
to its standard output and of its WAV file. Needs PyAV (pip install av);
the WAV file is read when the pictures have ended, so swvideo can still be
writing it while they come in.
"""
import sys
import wave
from fractions import Fraction

import av
import numpy as np


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    out, wav_path = sys.argv[1], sys.argv[2]
    width, height, fps = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    box = av.open(out, "w")
    video = box.add_stream("libx264", rate=fps)
    video.width, video.height = width, height
    video.pix_fmt = "yuv420p"
    video.options = {"crf": "17", "preset": "medium"}
    rate = 44100                # of the sound swvideo writes
    audio = box.add_stream("aac", rate=rate)
    audio.bit_rate = 160000
    size = width * height * 3
    pictures = 0
    while True:
        buf = sys.stdin.buffer.read(size)
        if len(buf) < size:
            break
        frame = av.VideoFrame.from_ndarray(np.frombuffer(buf, np.uint8).reshape(height, width, 3), format="rgb24")
        frame.pts = pictures
        frame.time_base = Fraction(1, fps)
        for packet in video.encode(frame):
            box.mux(packet)
        pictures += 1
    for packet in video.encode():
        box.mux(packet)

    with wave.open(wav_path, "rb") as w:
        if w.getframerate() != rate or w.getnchannels() != 1:
            sys.exit("mux.py: the sound has to be %d Hz mono" % rate)
        pcm = np.frombuffer(w.readframes(w.getnframes()), np.int16)
    resample = av.AudioResampler(format="fltp", layout="mono", rate=rate)
    step = 1024
    for i in range(0, len(pcm), step):
        chunk = pcm[i:i + step]
        frame = av.AudioFrame.from_ndarray(chunk.reshape(1, -1), format="s16", layout="mono")
        frame.sample_rate = rate
        frame.pts = i
        frame.time_base = Fraction(1, rate)
        for converted in resample.resample(frame):
            for packet in audio.encode(converted):
                box.mux(packet)
    for packet in audio.encode():
        box.mux(packet)
    box.close()
    print("mux.py: %d pictures, %.1f s of sound -> %s" % (pictures, len(pcm) / rate, out))


if __name__ == "__main__":
    main()
