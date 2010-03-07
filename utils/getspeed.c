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

int main(int argc, char *argv[])
{
	int sock;
	struct dirent **namelist = NULL;
	struct sockaddr_un sck;
	struct stat st;
	time_t last_mtime = 0;
	unsigned int cmd = 0;
	unsigned int full_cmd = 0;
	unsigned int i = 0;
	char buf[4096] = {0}, name[256] = {0}, policy[255] = {0};
	char *in;
	int min, max, active, n;

	if (argc == 2 && !strcmp(argv[1], "-l"))
		cmd = CMD_CUR_PROFILES;
	else if (argc == 1)
		cmd = CMD_LIST_PROFILES;
	else {
		fprintf(stdout, "%s: Wrong arguments\n", argv[0]);
		fprintf(stdout, "%s [-l]\n", argv[0]);
		fprintf(stdout, "	-l  list applied profiles\n");
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

	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()");
		return 1;
	}

	if (connect(sock, (struct sockaddr *)&sck, sizeof(sck)) == -1) {
		perror("connect()");
		return 1;
	}
	
	full_cmd = MAKE_COMMAND(cmd, 0);
	if (write(sock, &full_cmd, 4) != 4)
		perror("write()");

	n = 0;
	while (read(sock, buf, 4096)) {
		int is_active = 0;
		in = buf;
		while (cmd == CMD_LIST_PROFILES 
				&& sscanf(in, "%d/%[^/]/%d/%d/%[^\n]\n", &active, name, &min, &max, policy) == 5) {
			printf("\nName (#%d):\t%s\n", ++n, name);
			/* pretty print active cpus */
			is_active = 0;
			for (i = 0; i < sizeof(active); i++) {
				if (active & (1 << i)) {
					if (is_active == 0)
						printf("Active on CPU#:\t");
					else if (is_active > 0)
						printf(", ");
					printf("%d", i);
					is_active++;
				}
			}
			if (is_active)
				printf("\n");

			printf("Governor:\t%s\n", policy);
			printf("Min freq:\t%d\n", min);
			printf("Max freq:\t%d\n", max);
			in = strchr(in, '\n') + 1;
		}

		while (cmd == CMD_CUR_PROFILES
				&& sscanf(in, "%d/%[^/]/%d/%d/%[^\n]\n", &active, name, &min, &max, policy) == 5) {
			printf("\nCPU#%d: \"%s\" ", active, name);
			printf("%s %d-%d\n", policy, min, max);
			in = strchr(in, '\n') + 1;
		}
	}

	close(sock);

	return 0;
}
