/*
 * srd-test - measures srd latency on the device.
 *
 * usage: srd-test SOCKET VOICE TEXT [INTERRUPT_MS TEXT2]
 *
 * Prints the time to the first audio frame and to the end of the utterance
 * (the second one, if INTERRUPT_MS is given), and how much audio of each
 * utterance arrived.
 */

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static int read_all(int fd, void *buf, size_t len)
{
	char *p = buf;

	while (len > 0) {
		ssize_t n = read(fd, p, len);
		if (n <= 0) {
			return -1;
		}
		p += n;
		len -= n;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct sockaddr_un addr;
	char line[4200], data[65536];
	long t0, first = -1, interrupt_ms = -1;
	unsigned long bytes[3] = { 0, 0, 0 };
	uint32_t want = 1, header[2];
	int fd, sent = 0;

	if (argc < 4) {
		fprintf(stderr, "usage: %s SOCKET VOICE TEXT [INTERRUPT_MS TEXT2]\n", argv[0]);
		return 1;
	}
	if (argc >= 6) {
		interrupt_ms = atol(argv[4]);
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect");
		return 1;
	}

	t0 = now_ms();
	snprintf(line, sizeof(line), "S 1 %s 175 50 100 %s\n", argv[2], argv[3]);
	write(fd, line, strlen(line));

	while (1) {
		struct pollfd pfd = { fd, POLLIN, 0 };

		if (interrupt_ms >= 0 && !sent && now_ms() - t0 >= interrupt_ms) {
			snprintf(line, sizeof(line), "S 2 %s 175 50 100 %s\n", argv[2], argv[5]);
			write(fd, line, strlen(line));
			sent = 1;
			want = 2;
			printf("unterbrochen nach %ld ms\n", now_ms() - t0);
		}
		if (poll(&pfd, 1, 10) <= 0) {
			continue;
		}
		if (read_all(fd, header, sizeof(header)) < 0 || header[1] > sizeof(data)
		    || read_all(fd, data, header[1]) < 0) {
			break;
		}
		if (header[1] && first < 0) {
			first = now_ms() - t0;
		}
		if (header[0] < 3) {
			bytes[header[0]] += header[1];
		}
		if (header[0] == want && header[1] == 0) {
			break;
		}
	}

	printf("erstes Audio nach %ld ms, fertig nach %ld ms\n", first, now_ms() - t0);
	printf("Ansage 1: %.2f s Audio, Ansage 2: %.2f s Audio\n",
	       bytes[1] / 2 / 44100.0, bytes[2] / 2 / 44100.0);
	close(fd);
	return 0;
}
