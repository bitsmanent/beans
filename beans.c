/* beans is a simple pastebin server */

#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "arg.h"

#define BACKLOG 32

/* function declarations */
void die(const char *errstr, ...);
int bindon(char *port);
void *ecalloc(size_t nmemb, size_t size);
char *readall(int sd, int *len);
void run(void);
void serve(int sd);
void sout(int sd, char *fmt, ...);

/* variables */
char *argv0;
char port[8] = "2023";
char base[256] = "";
char path[256] = "/tmp";
char mode[8] = "0600";
int sockd;

/* function implementations */
void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

int
bindon(char *port) {
	struct addrinfo hints, *res;
	int sd = 0, e;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if((e = getaddrinfo(NULL, port, &hints, &res)))
		die("getaddrinfo(): %s\n", gai_strerror(e));
	if((sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1)
		die("socket(): %s\n", strerror(errno));
	if(bind(sd, res->ai_addr, res->ai_addrlen) == -1)
		die("bind(): %s\n", strerror(errno));
	if(listen(sd, BACKLOG) == -1)
		die("listen(): %s\n", strerror(errno));
	return sd;
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p;

	if(!(p = calloc(nmemb, size)))
		die("Cannot allocate memory.\n");
	return p;
}

char *
readall(int sd, int *len) {
	char *buf;
	int sz = 512, r, l = 0;

	buf = ecalloc(1, sz);
	while((r = read(sd, &buf[l], sz - l)) != -1) {
		if(!r)
			break;
		l += r;
		if(l == sz) {
			sz *= 2;
			if(!(buf = realloc(buf, sz)))
				die("realloc()\n");
		}
	}
	if(r == -1) {
		free(buf);
		return NULL;
	}
	if(len)
		*len = l;
	buf[l] = '\0';
	return buf;
}

void
run(void) {
	struct sockaddr_storage conn;
	socklen_t size;
	int csd;
	pid_t pid;

	while(1) {
		size = sizeof conn;
		csd = accept(sockd, (struct sockaddr *)&conn, &size);
		if(csd == -1) {
			fprintf(stderr, "accept(): %s", strerror(errno));
			continue;
		}
		pid = fork();
		if(pid) {
			if(pid == -1)
				fprintf(stderr, "fork(): %s\n", strerror(errno));
			close(csd);
			continue;
		}
		serve(csd);
		close(csd);
		break;
	}
}

void
serve(int sd) {
	int len, tmpsd;
	char *buf, *code;
	char tmpfn[64] = {0};

	buf = readall(sd, &len);
	if(!(buf && len)) {
		sout(sd, "Nothing pasted.\n");
		return;
	}
	snprintf(tmpfn, sizeof tmpfn, "%s/beans.XXXXXX", path);
	tmpsd = mkstemp(tmpfn);
	if(tmpsd == -1) {
		fprintf(stderr, "mkstemp()\n");
		free(buf);
		return;
	}
	if(write(tmpsd, buf, len) == -1)
		fprintf(stderr, "write(): %s\n", strerror(errno));
	code = strchr(tmpfn, '.')+1;
	if(*base)
		sout(sd, "%s%s\n", base, code);
	else
		sout(sd, "%s\n", code);
	fchmod(tmpsd, strtol(mode, 0, 8));
	close(tmpsd);
	free(buf);
}

void
sout(int sd, char *fmt, ...) {
	va_list ap;
	char buf[4096];
	int sz;

	va_start(ap, fmt);
	sz = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	send(sd, buf, sz, 0);
}

int
main(int argc, char *argv[]) {
	ARGBEGIN {
	case 'b': strncpy(base, EARGF(die("%s: missing base URL\n", argv0)), sizeof base); break;
	case 'd': strncpy(path, EARGF(die("%s: missing path\n", argv0)), sizeof path); break;
	case 'm': strncpy(mode, EARGF(die("%s: missing mode\n", argv0)), sizeof mode); break;
	case 'p': strncpy(port, EARGF(die("%s: missing port\n", argv0)), sizeof port); break;
	case 'v': die("beans-"VERSION"\n");
	} ARGEND

	sockd = bindon(port);
	signal(SIGCHLD, SIG_IGN); /* cleanup zombies */
	run();
	close(sockd);
	return 0;
}
