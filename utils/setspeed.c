#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

int main(int argc, char *argv[])
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	struct stat st;
	time_t last_mtime = 0;
	long int n = 0;
	unsigned int cmd = 0;
	char *endptr = NULL;
	char buf[108];
	

	if (argc != 2) {
		printf("usage: %s [manual | dynamic | <profile#>]\n", argv[0]);
		return 1;
	}
	
	sck.sun_family = AF_UNIX;
	sck.sun_path[0] = '\0';
	/* get path */
	n = scandir("/tmp", &namelist, &cpufreqd_dirs, NULL);
	if (n > 0) { 
		while (n--) {
			snprintf(buf, 108, "/tmp/%s", namelist[n]->d_name);
			free(namelist[n]);
			if (stat(buf, &st) != 0) {
				fprintf(stderr, "%s: %s\n", buf, strerror(errno));
				continue;
			}
#if 0
			fprintf(stdout, "%s %lu %lu %lu\n", buf, st.st_ctime, st.st_atime, st.st_mtime);
#endif
			if (last_mtime == 0 || last_mtime < st.st_mtime) {
				last_mtime = st.st_mtime;
				snprintf(sck.sun_path, 108,"%s/cpufreqd", buf);
#if 0
				fprintf(stdout, "--> %s\n", buf);
#endif
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
#if 0
	fprintf(stdout, "socket I'll try to connect: %s\n", sck.sun_path);
#endif


	if (!strcmp(argv[1], "dynamic"))
		cmd = MAKE_COMMAND(CMD_SET_MODE, MODE_DYNAMIC);
	else if (!strcmp(argv[1], "manual"))
		cmd = MAKE_COMMAND(CMD_SET_MODE, MODE_MANUAL);
	else {
		n = strtol(argv[1], &endptr, 10);
		if (errno == ERANGE) {
			fprintf (stderr, "Overflow in long int %ld (%s)\n", n, argv[1]);
			return ERANGE;
		}
		if (n >> 16) {
			fprintf (stderr, "Profile number out of range. Must be 0 < %s > %d\n",
					argv[1], 0xffff);
			return EINVAL;
		}
		if (endptr[0] != '\0') {
			fprintf (stderr, "Unknown argument %s\n", argv[1]);
			return EINVAL;
		}
		cmd = MAKE_COMMAND(CMD_SET_PROFILE, n);
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
