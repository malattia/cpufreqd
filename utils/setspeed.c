#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "cpufreqd_remote.h"

int main(int argc, char *argv[])
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	int n = 0;
	unsigned int cmd = 0;
	

	if (argc != 2) {
		printf("usage: %s [manual | dynamic]\n", argv[0]);
		return 1;
	}
	
	sck.sun_family = AF_UNIX;
	/* get path */
	n = scandir("/tmp", &namelist, NULL, NULL);
	if (n > 0) { 
		while (n--) {
			if (strncmp(namelist[n]->d_name, "cpufreqd-", 9) == 0) {
				snprintf(sck.sun_path, 512, "/tmp/%s/cpufreqd", namelist[n]->d_name);
				cmd = 1;
			}
			free(namelist[n]);
		}
	}
	if (cmd == 0) {
		return EINVAL;
	}
	fprintf(stdout, "socket I'll try to connect: %s\n", sck.sun_path);


	if (!strcmp(argv[1], "dynamic"))
		cmd = MAKE_COMMAND(CMD_SET_MODE, ARG_DYNAMIC);
	else if (!strcmp(argv[1], "manual"))
		cmd = MAKE_COMMAND(CMD_SET_MODE, ARG_MANUAL);
	else {
		fprintf (stderr, "Unknown command %s\n", argv[1]);
		return EINVAL;
	}

	fprintf(stdout,  "command: %.8x %.4x %.4x\n", cmd, REMOTE_CMD(cmd), REMOTE_ARG(cmd));
	
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()");
		return errno;
	}

	if (connect(sock, (struct sockaddr *)&sck, sizeof(sck)) == -1) {
		perror("connect()");
		close(sock);
		return errno;
	}

	if (write(sock, &cmd, 4) != 4)
		perror("write()");	

	close(sock);

	return 0;
}
