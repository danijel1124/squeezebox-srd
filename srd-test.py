#!/usr/bin/env python3
"""Test client for srd: speaks one text, reports latency, writes a wav.

Usage: srd-test.py SOCKET OUT.wav TEXT [VOICE] [INTERRUPT_AFTER_MS TEXT2]
"""

import socket
import struct
import sys
import time
import wave


def main():
    path, out, text = sys.argv[1], sys.argv[2], sys.argv[3]
    voice = sys.argv[4] if len(sys.argv) > 4 else "de"
    interrupt = None
    if len(sys.argv) > 6:
        interrupt = (int(sys.argv[5]) / 1000.0, sys.argv[6])

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    t0 = time.monotonic()
    s.sendall(("S 1 %s 175 50 100 %s\n" % (voice, text)).encode())

    pcm = bytearray()
    first = None
    want = 1
    sent_interrupt = False
    buf = b""
    while True:
        if interrupt and not sent_interrupt and time.monotonic() - t0 >= interrupt[0]:
            s.sendall(("S 2 %s 175 50 100 %s\n" % (voice, interrupt[1])).encode())
            sent_interrupt = True
            want = 2
        s.settimeout(0.05)
        try:
            data = s.recv(65536)
        except socket.timeout:
            continue
        if not data:
            break
        buf += data
        done = False
        while len(buf) >= 8:
            fid, length = struct.unpack("<II", buf[:8])
            if len(buf) < 8 + length:
                break
            chunk = buf[8:8 + length]
            buf = buf[8 + length:]
            if first is None and length:
                first = time.monotonic() - t0
            if fid == want:
                if length == 0:
                    done = True
                pcm += chunk
        if done:
            break

    total = time.monotonic() - t0
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(bytes(pcm))
    print("erstes Audio nach %.0f ms, fertig nach %.0f ms, %.2f s Sprache"
          % (first * 1000, total * 1000, len(pcm) / 2 / 44100))


if __name__ == "__main__":
    main()
