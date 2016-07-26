/*
 * e_loop helper program
 * Copyright (c) 2015, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


char *e_loop_cmd_file = "/data/local/hs2/To_Phone/tag_file";
char *e_loop_log_file = "/data/local/hs2/To_Phone/Logs/e_loop.log";
static const char *log_file = NULL;
static const char *tag_file = NULL;


int main(int argc, char *argv[])
{
	char *buf = NULL;
	char *cmd = NULL;
	long pos;
	int c, ret;
	size_t len = 0;
	FILE *f, *f2 = NULL;

	/* Set the defaults */
	log_file = e_loop_log_file;
	tag_file = e_loop_cmd_file;

	for (;;) {
		c = getopt(argc, argv, "l:t:");
		if (c < 0)
			break;
		switch (c) {
		case 'l':
			log_file = optarg;
			break;
		case 't':
			tag_file = optarg;
			break;
		default:
			printf("usage: e_loop [-l<log_filename>] [-t<tag_filename>]\n");
			exit(0);
			break;
		}
	}

	/* Main command event loop */
	while (1) {
		/* Wait for a tag_file with a command to process */
		while (!(f = fopen(tag_file, "rb")))
			sleep(1);

		len = 80;
		/* Figure out how long the file is */
		if (fseek(f, 0, SEEK_END) < 0 || (pos = ftell(f)) < 0) {
			fclose(f);
			return -1;
		}
		len = pos;
		if (fseek(f, 0, SEEK_SET) < 0) {
			fclose(f);
			return -1;
		}
		buf = malloc(len);
		if (!buf) {
			fclose(f);
			return -1;
		}
		/* Read up the command line */
		if (fread(buf, 1, len, f) != len) {
			fclose(f);
			free(buf);
			return -1;
		}
		fclose(f);

		buf[len - 1] = '\0';

		if (log_file) {
			len = strlen(buf) + strlen(log_file) + 7;
			cmd = malloc(len);
			if (cmd == NULL) {
				free(buf);
				return -1;
			}
			ret = snprintf(cmd, len, "%s > %s", buf, log_file);
			if (ret < 0 || (size_t) ret >= len) {
				free(buf);
				free(cmd);
				return -1;
			}
			free(buf);
			buf = NULL;
		} else {
			cmd = buf;
		}

		cmd[len - 1] = '\0';

		/*
		 * This string "cmd" will contain the command passed in by
		 * hs20-action.sh. And the name of the "logfile". Which can be
		 * monitored for the result.
		 */
		ret = system(cmd);

		if (WIFEXITED(ret)) {
			ret = WEXITSTATUS(ret);
		}

		if ((f2 = fopen(log_file, "a")) == NULL) {
			free(cmd);
			return -1;
		}

		if (fprintf(f2,"\nELOOP_CMD : %s\n", cmd) <= 0) {
			fclose(f2);
			free(cmd);
			return -1;
		}

		if (fprintf(f2,"\nELOOP_CMD_STATUS : %d\n", ret) <= 0) {
			fclose(f2);
			free(cmd);
			return -1;
		}

		/* Only free the cmd buffer. It is all that is left allocated */
		free(cmd);

		fclose(f2);

		/* Clean up */
		unlink(tag_file);
	}

	return ret;
}
