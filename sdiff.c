/* $Id$ */

#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"

#define HT_DEL 1
#define HT_ADD 2
#define HT_CHG 3
#define HT_ERR 4

struct hunk {
	int h_type;
	int h_as;	/* file a start */
	int h_ae;	/* file a end */
	int h_bs;	/* file b start */
	int h_be;	/* file b end */
};

static		char **buildargv(void);
static		char **buildenvp(void);
static		int    startdiff(void);
static		void   readhunk(const char *, struct hunk *);
static __dead	void   usage(void);

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

static char *passenv[] = {
	"TMPDIR",
	NULL
};

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
	char *lbd, *lba, *lbb, *lbdup = NULL;
	int c, fd, status, lna, lnb;
	FILE *fpd, *fpa, *fpb;
	struct hunk h;
	size_t siz;
	long l;

	while ((c = getopt_long(argc, argv, "aBbD:dEHI:ilo:SstWw:",
	    lopts, NULL)) != -1) {
		switch (c) {
		case 'a':
			ascii = 1;
			break;
		case 'B':
			errx(2, "-B not yet supported");

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
			errx(2, "-E not yet supported");

			ignoretab = 1;
			break;
		case 'H':
			errx(2, "-H not yet supported");

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
	argv += optind;
	argc -= optind;

	if (argc != 2)
		usage();

	/* XXX: race between now and when diff opens these files. */
	if ((fpa = fopen(argv[0], "r")) == NULL)
		err(2, "fopen %s", argv[0]);
	if ((fpb = fopen(argv[1], "r")) == NULL)
		err(2, "fopen %s", argv[1]);

	fd = startdiff();
	if ((fpd = fdopen(fd, "r")) == NULL)
		err(2, "fdopen %s", diffprog);
	lna = lnb = 0;
	while (!feof(fpd)) {
		if ((lbd = fgetln(fpd, &siz)) == NULL)
			err(2, "fgetln %s", diffprog);
		if (lbd[siz - 1] == '\n')
			lbd[siz - 1] = '\0';
		else {
			lbdup = malloc(siz + 1);
			memcpy(lbdup, lbd, siz);
			lbdup[siz] = '\0';
			lbd = lbdup;
		}

		readhunk(lbd, &h);
		switch (h.h_type) {
		case HT_DEL:
			break;
		case HT_ADD:
			break;
		case HT_CHG:
			break;
		case HT_ERR:
			err(2, "invalid diff output: %s", lbd);
			/* NOTREACHED */
		}
/*
	17,18d16
	< ums0 at uhidev0: 3 buttons and Z dir.
	< wsmouse0 at ums0 mux 0

	33a32
	> pfr_get_tables: corruption detected (3).

	36a36
	> pfr_get_tables: corruption detected (1).

	329c329
	< wsmouse0 at ums0 mux 0
	---
	> smouse0 at ums0 mux 0
*/

		free(lbdup);
		lbdup = NULL;
	}
	(void)fclose(fpd);
	(void)fclose(fpa);
	(void)fclose(fpb);
	(void)wait(&status);
	exit(status);
}

#define ST_BEG  1	/* beginning */
#define ST_NUM1 2	/* after number 1 */
#define ST_COM1 3	/* after comma 1 */
#define ST_NUM2 4	/* after number 2 */
#define ST_TYPE 5	/* after type */
#define ST_NUM3 6	/* after number 3 */
#define ST_COM2 7	/* after comma 2 */
#define ST_NUM4 8	/* after number 4 */

static void
readhunk(const char *s, struct hunk *h)
{
	const char *p;
	int i, state;

	h->h_as = 0;
	h->h_ae = 0;
	h->h_bs = 0;
	h->h_be = 0;

	state = ST_BEG;
	for (p = s; *p != '\0'; p++) {
		switch (*p) {
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		case '8': case '9':
			i = 0;
			while (isdigit(*p))
				i = (i * 10) + (*p++ - '0');
			switch (state) {
			case ST_BEG:
				state = ST_NUM1;
				h->h_as = i;
				break;
			case ST_COM1:
				state = ST_NUM2;
				h->h_ae = i;
				break;
			case ST_TYPE:
				state = ST_NUM3;
				h->h_bs = i;
				break;
			case ST_COM2:
				state = ST_NUM4;
				h->h_be = i;
				goto end;
				/* NOTREACHED */
			default:
				goto badhunk;
				/* NOTREACHED */
			}
			break;
		case 'a':
		case 'c':
		case 'd':
			/* These can skip ST_COM1 and ST_NUM2. */
			if (state != ST_NUM1 &&
			    state != ST_NUM2)
				goto badhunk;
			state = ST_TYPE;
			if (*p == 'a')
				h->h_type = HT_ADD;
			else if (*p == 'c')
				h->h_type = HT_CHG;
			else
				h->h_type = HT_DEL;
			break;
		case ',':
			if (state != ST_NUM1 &&
			    state != ST_NUM3)
				goto badhunk;
			if (state == ST_NUM1)
				state = ST_COM1;
			else
				state = ST_COM2;
			break;
		default:
			goto badhunk;
			/* NOTREACHED */
		}
	}

end:
	if (state != ST_NUM2 &&
	    state != ST_NUM4)
		goto badhunk;
	if (*p != '\0')
		goto badhunk;
	/* XXX: sanity check values. */
	return;

badhunk:
	h->h_type = HT_ERR;
}

static char **
buildargv(void)
{
	size_t siz;
	char **argv;
	int i;

	siz = 0;
	if (ignorere != NULL)
		siz += 2;
	siz += ascii + expandtabs + ignorecase + minimal + largefiles +
	       ignorewsamt + ignorews + ignoretab + ignoreblank;
	if (ignorere)
		siz += 2;

	if ((argv = calloc(siz, sizeof(*argv))) == NULL)
		err(2, "calloc");
	i = 0;
	if (ascii)
		argv[i++] = "-a";
	if (expandtabs)
		argv[i++] = "-t";
	if (ignorecase)
		argv[i++] = "-i";
	if (minimal)
		argv[i++] = "-d";
	if (largefiles)
		argv[i++] = "-H";
	if (ignorewsamt)
		argv[i++] = "-b";
	if (ignorews)
		argv[i++] = "-w";
	if (ignoretab)
		argv[i++] = "-E";
	if (ignoreblank)
		argv[i++] = "-B";
	if (ignorere != NULL) {
		argv[i++] = "-I";
		argv[i++] = ignorere;
	}
	argv[i] = NULL;
	return (argv);
}

static char **
buildenvp(void)
{
	char **ep, **fp, **envp;
	extern char **environ;
	size_t siz;
	int i;

	siz = 0;
	for (ep = environ; *ep != NULL; ep++) {
		for (fp = passenv; *fp != NULL; fp++)
		if (strncmp(*ep, *fp, strlen(*fp)) == 0 &&
		    (*ep)[strlen(*fp)] == '=') {
			siz++;
		}
	}
	if ((envp = calloc(siz, sizeof(*envp))) == NULL)
		err(2, "calloc");
	for (i = 0, ep = environ; *ep != NULL; ep++) {
		for (fp = passenv; *fp != NULL; fp++)
		if (strncmp(*ep, *fp, strlen(*fp)) == 0 &&
		    (*ep)[strlen(*fp)] == '=') {
			envp[i++] = *ep;
		}
	}
	envp[i] = NULL;
	return (envp);
}

static int
startdiff(void)
{
	char **argv, **envp;
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
		if (fclose(stdout) == -1)
			err(2, "fclose <stdout>");
		if (dup(fd[1]) == -1)
			err(2, "dup <pipe>");
		(void)execve(diffprog, argv, envp);
		free(argv);
		free(envp);
		err(2, "execve");
		/* NOTREACHED */
	default:
		(void)close(fd[1]);
		(void)fclose(stdin);
		free(argv);
		free(envp);
		break;
	}
	return (fd[0]);
}

static __dead void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s [-abdHilstW] [-I pattern] "
		"[-o file] [-w width] file1 file2\n", __progname);
	exit(2);
}
