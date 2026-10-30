/*
 * srd - screen reader speech daemon for the Squeezebox Radio
 *
 * Keeps eSpeak NG loaded and streams synthesized speech to one client
 * (jive) over a unix socket, so speech starts while it is still being
 * synthesized and no process is started per utterance.
 *
 * Copyright (C) 2026 danijel
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. It links eSpeak NG, which is GPL-3.0-or-later.
 *
 * Protocol, client to srd, one command per line (UTF-8):
 *   S <id> <voice> <rate> <pitch> <volume> <text>   speak, replaces current
 *   X                                               stop
 *
 * srd to client, frames: u32 id, u32 length, then length bytes of signed
 * 16 bit mono PCM at 44100 Hz (native byte order). A frame of length 0 ends
 * utterance id. Synthesis of an utterance is aborted as soon as a new
 * command arrives.
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <espeak-ng/speak_lib.h>

#define DEFAULT_SOCKET "/tmp/srd.sock"
#define LINE_MAX_LEN 4096
/* synthesis buffer in ms, small for a quick start of speech */
#define BUFFER_MS 60

static int client = -1;
/* set when writing to or reading from the client failed */
static int lost;
static uint32_t current_id;
static int16_t last_sample;

/* incoming command bytes, possibly several lines */
static char inbuf[LINE_MAX_LEN];
static size_t inlen;

/* set by the synth callback when a new command arrived */
static int interrupted;


static int write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len > 0) {
		ssize_t n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		p += n;
		len -= n;
	}
	return 0;
}


static int send_frame(uint32_t id, const int16_t *pcm, uint32_t samples)
{
	uint32_t header[2];

	header[0] = id;
	header[1] = samples * sizeof(int16_t);
	if (write_all(client, header, sizeof(header)) < 0) {
		return -1;
	}
	if (samples && write_all(client, pcm, samples * sizeof(int16_t)) < 0) {
		return -1;
	}
	return 0;
}


/* read whatever the client sent, without blocking. returns -1 when the
 * client is gone.
 */
static int read_input(int timeout_ms)
{
	struct pollfd pfd;
	ssize_t n;

	pfd.fd = client;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, timeout_ms) <= 0) {
		return 0;
	}

	if (inlen == sizeof(inbuf)) {
		/* line too long, drop it */
		inlen = 0;
	}
	n = read(client, inbuf + inlen, sizeof(inbuf) - inlen);
	if (n <= 0) {
		return -1;
	}
	inlen += n;
	return 0;
}


static int have_line(void)
{
	return memchr(inbuf, '\n', inlen) != NULL;
}


static int synth_callback(short *wav, int numsamples, espeak_EVENT *events)
{
	int16_t out[2 * 4096];
	int i, n = 0;

	(void) events;

	if (wav == NULL || lost) {
		return lost;
	}

	/* 22050 Hz to 44100 Hz, linear interpolation */
	for (i = 0; i < numsamples; i++) {
		out[n++] = (int16_t) (((int32_t) last_sample + wav[i]) / 2);
		out[n++] = wav[i];
		last_sample = wav[i];

		if (n == (int) (sizeof(out) / sizeof(out[0]))) {
			if (send_frame(current_id, out, n) < 0) {
				lost = 1;
				return 1;
			}
			n = 0;
		}
	}
	if (n && send_frame(current_id, out, n) < 0) {
		lost = 1;
		return 1;
	}

	/* a new command interrupts the current utterance */
	if (read_input(0) < 0) {
		lost = 1;
		return 1;
	}
	if (have_line()) {
		interrupted = 1;
		return 1;
	}
	return 0;
}


static void speak(char *args)
{
	char voice[64];
	unsigned long id;
	int rate, pitch, volume, offset = 0;
	char *text;

	if (sscanf(args, "%lu %63s %d %d %d %n", &id, voice, &rate, &pitch, &volume, &offset) < 5 || offset == 0) {
		return;
	}
	text = args + offset;

	current_id = (uint32_t) id;
	last_sample = 0;
	interrupted = 0;

	espeak_SetVoiceByName(voice);
	espeak_SetParameter(espeakRATE, rate, 0);
	espeak_SetParameter(espeakPITCH, pitch, 0);
	espeak_SetParameter(espeakVOLUME, volume, 0);

	espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);

	if (!interrupted && !lost) {
		/* end of utterance */
		if (send_frame(current_id, NULL, 0) < 0) {
			lost = 1;
		}
	}
}


/* handle all complete lines; only the last speak command matters */
static void handle_input(void)
{
	char *line, *nl, *last_speak = NULL;
	char cmd[LINE_MAX_LEN];

	line = inbuf;
	while ((nl = memchr(line, '\n', inlen - (line - inbuf))) != NULL) {
		*nl = '\0';
		if (line[0] == 'S' && line[1] == ' ') {
			last_speak = line + 2;
		}
		else if (line[0] == 'X') {
			last_speak = NULL;
		}
		line = nl + 1;
	}

	cmd[0] = '\0';
	if (last_speak) {
		strncpy(cmd, last_speak, sizeof(cmd) - 1);
		cmd[sizeof(cmd) - 1] = '\0';
	}

	/* keep an incomplete last line */
	inlen -= line - inbuf;
	memmove(inbuf, line, inlen);

	if (cmd[0]) {
		speak(cmd);
	}
}


static int listen_socket(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	unlink(path);

	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0 || listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}


int main(int argc, char **argv)
{
	const char *data = NULL, *path = DEFAULT_SOCKET;
	int server, opt;

	while ((opt = getopt(argc, argv, "d:s:")) != -1) {
		switch (opt) {
		case 'd':
			data = optarg;
			break;
		case 's':
			path = optarg;
			break;
		default:
			fprintf(stderr, "usage: %s [-d espeak-ng data dir] [-s socket]\n", argv[0]);
			return 1;
		}
	}

	signal(SIGPIPE, SIG_IGN);

	/* listen first, so a client can connect and send its first command
	 * while eSpeak NG is still loading
	 */
	server = listen_socket(path);
	if (server < 0) {
		perror("srd: socket");
		return 1;
	}

	if (espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, BUFFER_MS, data, espeakINITIALIZE_DONT_EXIT) < 0) {
		fprintf(stderr, "srd: cannot initialize eSpeak NG\n");
		return 1;
	}
	espeak_SetSynthCallback(synth_callback);

	while (1) {
		client = accept(server, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("srd: accept");
			return 1;
		}
		inlen = 0;
		lost = 0;

		while (!lost) {
			if (!have_line() && read_input(-1) < 0) {
				break;
			}
			handle_input();
		}

		close(client);
		client = -1;
	}

	return 0;
}
