# srd – Screen reader speech daemon for the Squeezebox Radio

srd keeps eSpeak NG loaded and streams synthesized speech to jive (SqueezePlay)
over a unix socket. jive mixes it into the speech channel of its effects mixer
as it arrives, so speech starts within a few tens of milliseconds and a new
utterance interrupts the current one immediately.

srd is a separate program because eSpeak NG is GPL-3.0-or-later, while jive
links libraries that are not GPL compatible. srd itself is GPL-3.0-or-later.

## Protocol

Client to srd, one command per line (UTF-8):

    S <id> <voice> <rate> <pitch> <volume> <text>   speak, replaces the current utterance
    X                                               stop

srd to client, frames: `u32 id`, `u32 length`, then `length` bytes of signed
16 bit mono PCM at 44100 Hz (native byte order). A frame of length 0 ends
utterance `id`. Synthesis is aborted as soon as a new command arrives.

srd listens on its socket before it loads eSpeak NG, so a client can connect
and send its first command right away.

    srd [-d <directory containing espeak-ng-data>] [-s <socket>]   (default /tmp/srd.sock)

## Building for the Radio

Toolchain: Sourcery G++ Lite 2010q1-202 (gcc 4.4.1, glibc 2.11), as used for
the SqueezeOS community firmware.

eSpeak NG 1.52.0, static library, without the optional parts. gcc 4.4 predates
`__has_include`, `gcc44-compat.h` fills in for it:

    ./autogen.sh
    ./configure --host=arm-none-linux-gnueabi --prefix=/usr --disable-shared \
        --without-pcaudiolib --without-sonic --without-speechplayer \
        --without-mbrola --without-async \
        CFLAGS="-Os -march=armv5te -mtune=arm926ej-s -marm -mthumb-interwork" \
        CPPFLAGS="-include /path/to/gcc44-compat.h"
    make src/libespeak-ng.la

srd:

    arm-none-linux-gnueabi-gcc -std=gnu99 -Os -march=armv5te -mtune=arm926ej-s \
        -marm -mthumb-interwork -Wall -Wextra -I<espeak-ng>/src/include \
        -o srd srd.c <espeak-ng>/src/.libs/libespeak-ng.a -lm -lpthread -Wl,--as-needed

The eSpeak NG data (phondata and the dictionaries) is built on the host with a
native eSpeak NG of the same version. For the 13 jive UI languages: `make`
the targets `espeak-ng-data/{cs,da,de,en,es,fi,fr,it,nl,no,pl,ru,sv}_dict`
after `phsource/phonemes.stamp`. Configure the host build with
`--without-extdict-ru`: the extended Russian dictionary is 8.5 MB, too much
RAM for the Radio.

## Testing

`srd-test.c` (for the device) and `srd-test.py` (for a host build) speak a
text, report the time to the first audio frame and can interrupt it with a
second text.
