/* $Id$ */

#include <err.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "pathnames.h"

static __dead void usage(void);

static char *diffprog = _PATH_DIFF;
static char *ignorere = NULL;
static char *outfile = NULL;
static int ascii = 0;
static int expandtabs = 0;
static int ignorecase = 0;
static int ignoretab = 0;
static int ignoreblank = 0;
static int ignorews = 0;
static int ignorewsamt = 0;
static int largefiles = 0;
static int leftcol = 0;
static int minimal = 0;
static int stripcr = 0;
static int suppresscommon = 0;
static int width = 0;

static struct option lopts[] = {
	{ "diff-program",		required_argument,	NULL, 'D' },
	{ "expand-tabs",		no_argument,		NULL, 't' },
	{ "ignore-all-space",		no_argument,		NULL, 'W' },
	{ "ignore-blank-lines",		no_argument,		NULL, 'B' },
	{ "ignore-case",		no_argument,		NULL, 'i' },
	{ "ignore-matching-lines",	required_argument,	NULL, 'I' },
	{ "ignore-space-change",	no_argument,		NULL, 'b' },
	{ "ignore-tab-expansion",	no_argument,		NULL, 'E' },
	{ "left-column",		no_argument,		NULL, 'l' },
	{ "minimal",			no_argument,		NULL, 'd' },
	{ "output",			required_argument,	NULL, 'o' },
	{ "speed-large-files",		no_argument,		NULL, 'H' },
	{ "strip-trailing-cr",		no_argument,		NULL, 'S' },
	{ "suppress-common-lines",	no_argument,		NULL, 's' },
	{ "text",			no_argument,		NULL, 'a' },
	{ "width",			required_argument,	NULL, 'w' },
	{ NULL,				0,			NULL, '\0' }
};

int
main(int argc, char *argv[])
{
	size_t siz;
	long l;
	int c;

	while ((c = getopt_long(argc, argv, "aBbD:dEHI:ilo:SstWw:",
	    lopts, NULL)) != -1) {
		switch (c) {
		case 'a':
			ascii = 1;
			break;
		case 'B':
			ignoreblank = 1;
			break;
		case 'b':
			ignorewsamt = 1;
			break;
		case 'D':
			diffprog = optarg;
			break;
		case 'd':
			minimal = 1;
			break;
		case 'E':
			ignoretab = 1;
			break;
		case 'H':
			largefiles = 1;
			break;
		case 'I':
			if (ignorere == NULL)
				siz = 1; /* NUL */
			else
				siz = strlen(ignorere);
			siz += strlen(optarg) + 1; /* | */
			if ((ignorere = realloc(ignorere, siz)) == NULL)
				err(2, "realloc");
			strlcat(ignorere, "|", siz);
			strlcat(ignorere, optarg, siz);
			break;
		case 'i':
			ignorecase = 1;
			break;
		case 'l':
			leftcol = 1;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'S':
			stripcr = 1;
			break;
		case 's':
			suppresscommon = 1;
			break;
		case 't':
			expandtabs = 1;
			break;
		case 'W':
			ignorews = 1;
			break;
		case 'w':
			if ((l = strtoul(optarg, NULL, 10)) < 0 ||
			    l > INT_MAX)
				errx(2, "%s: invalid width", optarg);
			width = (int)l;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
}

static const char *
buildargv(void)
{
	const char *argv;

	if ((argv = calloc()) == NULL)
		err(2, "calloc");
	
	return (argv);
}

static const char *
buildenvp(void)
{
	extern char **environ;
}

int
rundiff(void)
{
	const char **argv, **envp;
	int fd[2];

	argv = buildargv();
	envp = buildenvp();

	if (pipe(fd) == -1)
		err(2, "pipe");

	switch (fork()) {
	case -1:
		err(2, "fork");
		/* NOTREACHED */
	case 0:
		(void)close(fd[0]);
		if (close(fileno(STDOUT_FILENO)) == -1)
			err(2, "close <stdout>");
		if (dup(fd[1]) == -1)
			err(2, "dup <pipe>");
		(void)execve(diffprog, argv, envp);
		free(argv);
		free(envp);
		err(2, "execve");
		/* NOTREACHED */
	default:
		(void)close(fd[1]);
		(void)close(fileno(STDIN_FILENO));
		free(argv);
		free(envp);
		break;
	}
	return (fd[0]);
}

static __dead void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-aBbdEHilstW] [-I pattern] "
		"[-o file] [-w width] file1 file2\n");
	exit(2);
}
