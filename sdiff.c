/* $Id$ */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"

#define DEFWIDTH 130

/* Hunk types. */
#define HT_DEL 1
#define HT_ADD 2
#define HT_CHG 3
#define HT_ERR 4

struct hunk {
	int h_type;
	int h_a;	/* file a start */
	int h_b;	/* file a end */
	int h_c;	/* file b start */
	int h_d;	/* file b end */
};

static		char  *getline(FILE *);
static		char **buildargv(char *, char *, char *);
static		char **buildenvp(void);
static		int    startdiff(char *, char *);
static		void   disp(FILE *, const char *, const char *, char);
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
	FILE *fpd, *fpa, *fpb, *outfp;
	int c, fd, status, lna, lnb;
	char *lbd, *lba, *lbb;
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

	if (width < 4) {
		struct winsize ws;
		char *tty;
		int ttyfd;

		/* Guess width. */
		if ((tty = ttyname(STDIN_FILENO)) != NULL) {
			if ((ttyfd = open(tty, O_RDONLY)) != -1) {
				if (ioctl(ttyfd, TIOCSWINSZ, &ws) != -1)
					width = ws.ws_col;
				(void)close(ttyfd);
			}
		}
		errno = 0;
		if (width < 4)
			width = DEFWIDTH;
	}
	/* Width for each display, excluding gap in middle. */
	width = (width - 3) / 2;

	if (outfile == NULL)
		outfp = stdout;
	else
		if ((outfp = fopen(outfile, "w")) == NULL)
			err(2, "fopen %s", outfile);

	/* XXX: race between now and when diff opens these files. */
	if ((fpa = fopen(argv[0], "r")) == NULL)
		err(2, "fopen %s", argv[0]);
	if ((fpb = fopen(argv[1], "r")) == NULL)
		err(2, "fopen %s", argv[1]);

	fd = startdiff(argv[0], argv[1]);
	if ((fpd = fdopen(fd, "r")) == NULL)
		err(2, "fdopen %s", diffprog);
	lna = lnb = 0;
	while ((lbd = getline(fpd)) != NULL) {
		readhunk(lbd, &h);
		while (lna < h.h_a || lnb < h.h_b) {
			lba = NULL;
			lbb = NULL;
			if (lna < h.h_a) {
				lba = getline(fpa);
				lna++;
			}
			if (lnb < h.h_c) {
				lbb = getline(fpb);
				lnb++;
			}
			if (lba && lbb) {
				if (!suppresscommon ||
				    strcmp(lba, lbb) != 0)
					disp(outfp, lba, lbb, ' ');
			}
			free(lba);
			free(lbb);
		}
		switch (h.h_type) {
		case HT_ADD:
			while (h.h_a <= h.h_b) {
				lba = getline(fpa);
				disp(outfp, lba, "", '>');
				lna++;
				free(lba);
			}
			break;
		case HT_CHG:
			break;
		case HT_DEL:
			while (h.h_c <= h.h_d) {
				lbb = getline(fpa);
				disp(outfp, "", lba, '>');
				lnb++;
				free(lbb);
			}
			break;
		case HT_ERR:
			errx(2, "invalid diff output: %s", lbd);
			/* NOTREACHED */
		}
/*
	XXaYY[,ZZ]
	XXdYY[,ZZ]
	xx[,YY]cZZ[,QQ]

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

		free(lbd);
	}
	(void)fclose(fpd);
	(void)fclose(fpa);
	(void)fclose(fpb);

	if (outfp != stdout)
		(void)fclose(outfp);

	status = EXIT_SUCCESS;
	(void)wait(&status);
	exit(status);
}

static void
disp(FILE *fp, const char *a, const char *b, char c)
{
	(void)fprintf(fp, "%*.*s %c %*.*s\n", width, width, a, c, width,
	    width, b);
}

static char *
getline(FILE *fp)
{
	char *lb, *lbdup = NULL;
	size_t siz;

	if ((lb = fgetln(fp, &siz)) == NULL) {
		if (feof(fp))
			return (NULL);
		else
			err(2, "fgetln");
	}
	if (lb[siz - 1] == '\n') {
		lb[siz - 1] = '\0';
		if ((lb = strdup(lb)) == NULL)
			err(2, "strdup");
	} else {
		lbdup = malloc(siz + 1);
		memcpy(lbdup, lb, siz);
		lbdup[siz] = '\0';
		lb = lbdup;
	}
	return (lb);
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

	h->h_a = 0;
	h->h_b = 0;
	h->h_c = 0;
	h->h_d = 0;

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
				h->h_a = i;
				break;
			case ST_COM1:
				state = ST_NUM2;
				h->h_b = i;
				break;
			case ST_TYPE:
				state = ST_NUM3;
				h->h_c = i;
				break;
			case ST_COM2:
				state = ST_NUM4;
				h->h_d = i;
				goto end;
				/* NOTREACHED */
			default:
				goto badhunk;
				/* NOTREACHED */
			}
			p--;
			break;
		case 'a':
		case 'd':
			/* These can never be in ST_NUM2. */
			if (state == ST_NUM2)
				goto badhunk;
			/* FALLTHROUGH */
		case 'c':
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
	if (state != ST_NUM3 &&
	    state != ST_NUM4)
		goto badhunk;
	if (*p != '\0')
		goto badhunk;
	switch (h->h_type) {
	case HT_ADD:
		/* XXaYY[,ZZ] */
		if (!h->h_c)
			h->h_c = h->h_b;
		break;
	case HT_DEL:
		/* XXdYY[,ZZ] */
		if (!h->h_b)
			h->h_b = h->h_a;
		break;
	case HT_CHG:
		/* XX[,YY]cZZ[,QQ] */
		if (!h->h_b)
			h->h_b = h->h_a;
		if (!h->h_d)
			h->h_d = h->h_c;
		break;
	}
	/* Sanity check values. */
	if (h->h_b > h->h_a ||
	    h->h_d > h->h_c)
		goto badhunk;
	return;

badhunk:
	h->h_type = HT_ERR;
}

static char **
buildargv(char *d, char *a, char *b)
{
	size_t siz;
	char **argv;
	int i;

	siz = 4;
	if (ignorere != NULL)
		siz += 2;
	siz += ascii + expandtabs + ignorecase + minimal + largefiles +
	       ignorewsamt + ignorews + ignoretab + ignoreblank;
	if (ignorere)
		siz += 2;

	if ((argv = calloc(siz, sizeof(*argv))) == NULL)
		err(2, "calloc");
	i = 0;
	argv[i++] = d;
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
	argv[i++] = a;
	argv[i++] = b;
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

	siz = 1;
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
startdiff(char *a, char *b)
{
	char **argv, **envp, *dpn;
	int fd[2];

	if ((dpn = strrchr(diffprog, '/')) == NULL)
		dpn = diffprog;
	else
		dpn++;

	argv = buildargv(dpn, a, b);
	envp = buildenvp();

	if (pipe(fd) == -1)
		err(2, "pipe");

	switch (fork()) {
	case -1:
		err(2, "fork");
		/* NOTREACHED */
	case 0:
		(void)close(fd[0]);
		if (close(STDOUT_FILENO) == -1)
			err(2, "close <stdout>");
		if (dup(fd[1]) == -1)
			err(2, "dup <pipe>");
		(void)close(fd[1]);
		(void)execve(diffprog, argv, envp);
		free(argv);
		free(envp);
		err(2, "execve");
		/* NOTREACHED */
	default:
		(void)close(fd[1]);
		(void)close(STDIN_FILENO);
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
