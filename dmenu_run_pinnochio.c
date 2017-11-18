/* -*- Mode: C; encoding: us-ascii; -*-
 *
 *   Time-stamp: <2014-06-16 17:36:38 IST>
 *   Touched: Tue Mar 27 10:48:37 2012 +0530 <enometh@meer.net>
 *   Bugs-To: enometh@meer.net
 *   Status: Experimental.  Do not redistribute
 *   Copyright (C) 2012 Madhu.  All Rights Reserved.
 *
 * dmenu_run_pinnochio.c:
 *
 * SYNOPSIS
 *
 * Calls dmenu, lists the contents of a DMENU_CACHE history file, and
 * runs the result via SHELL. The history file is updated inplace with
 * unique commands, most recent commands appear first.
 *
 * ENVIRONMENT
 */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <err.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

char *
getenv_def(char *ident, char *default_value)
{
	char *ret;
	if ((ret = getenv(ident)) == NULL)
		return default_value;
	return ret;
}

char *
merge_filename(char *root, char *filenamestring)
{
	char *ret, DIRSEP = '/';
	size_t len1 = strlen(root), len2 = strlen(filenamestring);
	int wantsep = ((len1 > 0) && !(root[len1 - 1] == DIRSEP));
	size_t len3 = len1 + (wantsep?1:0) + len2;

	if ((ret = (char *) malloc(len3 + 1)) == NULL)
		err(EXIT_FAILURE, "malloc");
	strncpy(ret, root, len1);
	if (wantsep) ret[len1] = DIRSEP;
	strncpy(ret+len1+(wantsep?1:0), filenamestring, len2);
	ret[len3] = '\0';
	return ret;
}


void
bury(int sig)
{
	/* This is called to handle SIGCHLD. Wait3 lets BSD lay the
	 * dead child process to rest. Otherwise it hangs around until
	 * the server dies. During development this can eat up a lot
	 * of swap space.
	 */
	int status;
	wait3(&status, WNOHANG, NULL);
}

int
main(int argc, char **argv)
{
	char *cache_path = getenv_def("DMENU_CACHE", merge_filename(
					      getenv_def("HOME", ""),
					      getenv_def("DMENU_CACHE_FNAME",
							 ".dmenu_cache_2")));

	//warnx("using history file %s", cache_path);

	int fd, out[2], cpid;
	if ((fd = open(cache_path, O_RDWR|O_CREAT, S_IWUSR|S_IRUSR)) == -1)
		err(EXIT_FAILURE, "open(%s)", cache_path);

	struct stat stbuf;
	if (fstat(fd, &stbuf)) err(EXIT_FAILURE, "fstat(%s)", cache_path);
	size_t length = stbuf.st_size; //later size of mmaped area

//	signal(SIGCHLD, bury);
//	signal(SIGPIPE, SIG_IGN);

	if (pipe(out) == -1)		err(EXIT_FAILURE, "pipe(out)");
	if ((cpid = fork()) == -1)	err(EXIT_FAILURE, "fork");

	if (cpid == 0) {   /* Child reads from FD and writes to OUT */
		close(out[0]);
		if (dup2(fd, STDIN_FILENO) == -1)
			err(EXIT_FAILURE, "child:dup2(fd=%d,stdin=%d)",
			    fd, STDIN_FILENO);
		if (dup2(out[1], STDOUT_FILENO) == -1)
			err(EXIT_FAILURE, "child:dup2(out[1]=%d,stdout=%d)",
			    out[1], STDOUT_FILENO);

		if (execvp("dmenu", argv))
			err(EXIT_FAILURE, "execvp dmenu failed");
	} /* Child */

	// Parent reads from OUT
	close(out[1]);
	wait(NULL);

	char cmd[1024];	size_t nread;
	if ((nread = read(out[0], cmd, sizeof(cmd)-1)) == -1)
		err(EXIT_FAILURE, "read from dmenu failed");

	cmd[nread] = '\0';
	close(out[0]);

	if (nread==0 || (nread == 1 && cmd[0] == '\n'))
		errx(EXIT_SUCCESS, "ignoring cmd<<%s>>", cmd); /* KLUDGE */

	if (cmd[nread - 1] != '\n')
		errx(EXIT_FAILURE, "cmd: %s: expected newline terminated\n",
		     cmd);

	if (!length) {
		if (write(fd, cmd, nread) == -1)
			err(EXIT_FAILURE, "writing history");
		goto skip_mmap;
	}

	char *addr = NULL;
	const off_t offset = 0;
	if ((addr = mmap(NULL, length, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, offset)) == MAP_FAILED)
		err(EXIT_FAILURE, "mmap(%s,length=%d,fd=%d)",
		    cache_path, length, fd);

	// unconditionally terminate the history file with a newline
	if (addr[length - 1] != '\n') {
		if ((addr = (char *) mremap(addr, length, length + 1, 0))
		    == MAP_FAILED)
			err(EXIT_FAILURE, "mremap failed");
		addr[length] = '\n';
		length = length + 1;
		if (ftruncate(fd, length) == -1)
			err(EXIT_FAILURE, "fruncate(%s)", cache_path);
	}

	char *needle = strstr(addr, cmd);

	// skip substrings not matched at beginning of line
	if (needle != addr)
		while (needle != NULL && (*(needle-1) != '\n'))
			needle = strstr(needle + nread, cmd);

	if (needle == NULL) {
		size_t new_length = nread + length;
		if (ftruncate(fd, new_length) == -1)
			err(EXIT_FAILURE, "fruncate(%s)", cache_path);
		if ((addr = (char *) mremap(addr, length, new_length, 0))
		    == MAP_FAILED)
			err(EXIT_FAILURE, "mremap failed");
		memmove(addr + nread, addr, length);
		memmove(addr, cmd, nread);
		length = new_length;
	} else if (needle == addr) {
		//  v-----------------v------------v--------------v
		// addr             needle    needle+nread   addr+length
		//  v-------v------------v---------
		// addr addr+nread  needle+nread
	} else {
		memmove(addr + nread, addr, needle - addr);
		memmove(addr, cmd, nread);
	}

	if (munmap(addr, length) == -1)
		err(EXIT_FAILURE, "mmunmap(%s,%d)", cache_path, length);

skip_mmap:

	if (close(fd) == -1)
		err(EXIT_FAILURE, "close(%s)", cache_path);

	char *shell = getenv_def("SHELL", "/bin/sh");
	if (execl(shell, shell, "-c", cmd, NULL))
		err(EXIT_FAILURE, "execl");
}

/*
 * Local Variables:
 * c-file-style:"bsd"
 * c-basic-offset:8
 * c-toggle-electric-state:-1;
 * compile-command:(format "gcc -ggdb -o %s %s" (file-name-sans-extension (file-relative-name buffer-file-name)) (file-relative-name buffer-file-name))
 * End:
 */
