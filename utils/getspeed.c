#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include "cpufreqd_remote.h"

static int cpufreqd_dirs(const struct dirent *d) {
	return (strncmp(d->d_name, "cpufreqd-", 9) == 0);
}

int main(void)
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	struct timeval tv[2], curtv = { 0L, 0L };
	unsigned int cmd = 0;
	char buf[4096] = {0}, name[256] = {0}, policy[255] = {0};
	char *in;
	int min, max, active, n;
	/*
	int ndx = 0, count, n;
	*/

	if (argc > 1) {
		fprintf(stdout, "%s: No arguments allowed\n", argv[0]);
		return 1;
	}
	
	sck.sun_family = AF_UNIX;
	sck.sun_path[0] = '\0';
	/* get path */
	n = scandir("/tmp", &namelist, &cpufreqd_dirs, NULL);
	if (n > 0) { 
		while (n--) {
			snprintf(buf, 108, "/tmp/%s/cpufreqd", namelist[n]->d_name);
			free(namelist[n]);
			if (utimes(buf, tv) != 0) {
				fprintf(stderr, "%s: %s\n", buf, strerror(errno));
				continue;
			}
			if (curtv.tv_sec == 0 || curtv.tv_sec > tv[1].tv_sec || 
					(curtv.tv_sec == tv[1].tv_sec && curtv.tv_usec > tv[1].tv_usec)) {
				curtv.tv_sec = tv[1].tv_sec;
				curtv.tv_usec = tv[1].tv_usec;
				strncpy(sck.sun_path, buf, 108);
			}
		}
		free(namelist);
	} else {
		fprintf(stderr, "No cpufreqd socket found\n");
		return ENOENT;
	}
	if (!sck.sun_path) {
		fprintf(stderr, "No cpufreqd socket found\n");
		return ENOENT;
	}
	fprintf(stdout, "socket I'll try to connect: %s\n", sck.sun_path);
	
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()");
		return 1;
	}

	if (connect(sock, (struct sockaddr *)&sck, sizeof(sck)) == -1) {
		perror("connect()");
		return 1;
	}

	cmd = MAKE_COMMAND(CMD_LIST_PROFILES, 0);
	
	if (write(sock, &cmd, 4) != 4)
		perror("write()");

	n = 0;
	while (read(sock, buf, 4096)) {
		in = buf;
		while (sscanf(in, "%d/%[^/]/%d/%d/%[^\n]\n", &active, name, &min, &max, policy) == 5) {
			printf("\nName (#%d):\t%s %s\n", ++n, name, active ? "*" : "");
			printf("Governor:\t%s\n", policy);
			printf("Min freq:\t%d\n", min);
			printf("Max freq:\t%d\n", max);
			in = strchr(in, '\n') + 1;
		}
	}

	close(sock);

	return 0;
}
