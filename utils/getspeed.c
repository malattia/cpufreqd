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
#include <sys/un.h>
#include "cpufreqd_remote.h"

int main(void)
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	unsigned int cmd = 0;
	char buf[4096] = {0}, name[256] = {0}, policy[255] = {0};
	char *in;
	int min, max, active, n;
	/*
	int ndx = 0, count, n;
	*/
	sck.sun_family = AF_UNIX;
	/* get path */
	n = scandir("/tmp", &namelist, NULL, NULL);
	if (n > 0) { 
		while (n--) {
			if (strncmp(namelist[n]->d_name, "cpufreqd-", 9) == 0) {
				snprintf(sck.sun_path, 512, "/tmp/%s/cpufreqd", namelist[n]->d_name);
			}
			free(namelist[n]);
		}
		fprintf(stdout, "socket I'll try to connect: %s\n", sck.sun_path);
	}
	
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()");
		return 1;
	}

	if (connect(sock, (struct sockaddr *)&sck, sizeof(sck)) == -1) {
		perror("connect()");
		return -1;
	}

	cmd = MAKE_COMMAND(CMD_LIST_PROFILES, 0);
	
	if (write(sock, &cmd, 4) != 4)
		perror("write()");

	n = 0;
	while (read(sock, buf, 4096)) {
		in = buf;
		while (sscanf(in, "%d/%[^/]/%d/%d/%[^\n]\n", &active, name, &min, &max, policy) == 5) {
			printf("\nName (#%d):\t%s %s\n", n++, name, active ? "*" : "");
			printf("Governor:\t%s\n", policy);
			printf("Min freq:\t%d\n", min);
			printf("Max freq:\t%d\n", max);
			in = strchr(in, '\n') + 1;
		}
	}

	close(sock);

	return 0;
}
