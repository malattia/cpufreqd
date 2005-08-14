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

int main(void)
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	struct pollfd fds;
	unsigned int cmd = 0x01000000; /* get cmd */
	char buff[4096] = {0}, name[256] = {0}, policy[255] = {0};
	char mode, *input;
	int cpu, min, max;
	int ndx = 0, count, n;

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

	if (write(sock, &cmd, 4) != 4)
		perror("write()");

	fds.fd = sock;
	fds.events = POLLIN;

	if (poll(&fds, 1, 2000) == 1) {
		read(sock, buff, 4096);
		input = buff;


		sscanf(input, "%c/", &mode);
		printf("Operation mode:\t%s\n\n", (mode == 'd') ? "dynamic" : "manual");
		input += 2;

		while (1) {
			if (sscanf(input, "%d/%[^/]/%[^/]/%d/%d/", &cpu, name, policy, &min, &max) != 5)
				break;

			printf("Name (#%d):\t%s (cpu%d)\n", ndx++, name, cpu);
			printf("Policy:\t\t%s\n", policy);
			printf("Min freq:\t%d\n", min);
			printf("Max freq:\t%d\n\n", max);

			count = 0;
			while (count++ < 5)
				input = strchr(input, '/') + 1;
		}
	}

	close(sock);

	return 0;
}
