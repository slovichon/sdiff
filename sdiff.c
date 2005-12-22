/* $Id$ */
/*
 * Written by Jared Yanovich <jaredy@openbsd.org>
 * Public domain
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <paths.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lbuf.h"

#define DEFWIDTH 130

#define _PATH_DIFF	"/usr/bin/diff"

/* Hunk types. */
#define HT_DEL 1
#define HT_ADD 2
#define HT_CHG 3

struct hunk {
	int h_type;
	int h_a;	/* file a start */
	int h_b;	/* file a end */
	int h_c;	/* file b start */
	int h_d;	/* file b end */
};

int	 appendline(FILE *, struct lbuf *);
int	 readhunk(const char *, struct hunk *);
int	 startdiff(char **);
void	 addarg(char ***, size_t *, char *);
void	 ask(struct lbuf *, const char *, const char *);
void	 disp(struct lbuf *, struct lbuf *, char);
void	 edit(struct lbuf *, const char *, const char *);
void	 merge(FILE *, struct lbuf *, struct lbuf *);
void	 save(FILE *, struct lbuf *);
void	 sdiff(int);
void	 usage(void);

FILE	*tmpfp = NULL;
char	 prompt[] = "% ";
char	 tmpfn[MAXPATHLEN];
char	*diffprog = _PATH_DIFF;
char	*outfn = NULL;
FILE	*outfp;
int	 leftcol = 0;
int	 stripcr = 0;
int	 suppresscommon = 0;
int	 width = 0;

struct option lopts[] = {
	{ "text",			no_argument,		NULL, 'a' },
	{ "ignore-space-change",	no_argument,		NULL, 'b' },
	{ "diff-program",		required_argument,	NULL, 'D' },
	{ "minimal",			no_argument,		NULL, 'd' },
	{ "ignore-matching-lines",	required_argument,	NULL, 'I' },
	{ "ignore-case",		no_argument,		NULL, 'i' },
	{ "left-column",		no_argument,		NULL, 'l' },
	{ "output",			required_argument,	NULL, 'o' },
	{ "expand-tabs",		no_argument,		NULL, 't' },
	{ "strip-trailing-cr",		no_argument,		NULL, 'S' },
	{ "suppress-common-lines",	no_argument,		NULL, 's' },
	{ "ignore-all-space",		no_argument,		NULL, 'W' },
	{ "width",			required_argument,	NULL, 'w' },
	{ NULL,				0,			NULL, '\0' }
};

void
addarg(char ***s, size_t *siz, char *p)
{
	char **t;

	if ((t = realloc(*s, sizeof(**s) * ++*siz)) == NULL)
		err(2, NULL);
	t[*siz - 1] = p;
	*s = t;
}

int
main(int argc, char *argv[])
{
	int c, fd, status;
	char **diffargs;
	size_t siz;
	long l;

	diffargs = NULL;
	outfp = NULL;
	siz = 0;
	/* Placeholder for program name. */
	addarg(&diffargs, &siz, NULL);
	addarg(&diffargs, &siz, "--full");
	while ((c = getopt_long(argc, argv, "abdI:ilo:stWw:", lopts,
	    NULL)) != -1) {
		switch (c) {
		case 'a':
			addarg(&diffargs, &siz, "-a");
			break;
		case 'b':
			addarg(&diffargs, &siz, "-b");
			break;
		case 'D':
			diffprog = optarg;
			break;
		case 'd':
			addarg(&diffargs, &siz, "-d");
			break;
		case 'I':
			addarg(&diffargs, &siz, argv[optind]);
			break;
		case 'i':
			addarg(&diffargs, &siz, "-i");
			break;
		case 'l':
			leftcol = 1;
			break;
		case 'o':
			outfn = optarg;
			break;
		case 'S':
			stripcr = 1;
			break;
		case 's':
			suppresscommon = 1;
			break;
		case 't':
			addarg(&diffargs, &siz, "-t");
			break;
		case 'W':
			addarg(&diffargs, &siz, "-w");
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
		if ((tty = ttyname(STDIN_FILENO)) != NULL)
			if ((ttyfd = open(tty, O_RDONLY)) != -1) {
				if (ioctl(ttyfd, TIOCGWINSZ, &ws) != -1)
					width = ws.ws_col;
				(void)close(ttyfd);
			}
		errno = 0;
		if (width < 4)
			width = DEFWIDTH;
	}
	/* Width for each display, excluding gap in middle. */
	width = (width - 3) / 2;

	if (outfn != NULL) {
		char *tmpdir;
		int tmpfd;

		if ((outfp = fopen(outfn, "w")) == NULL)
			err(2, "fopen %s", outfn);
		if ((tmpdir = getenv("TMPDIR")) == NULL)
			tmpdir = _PATH_TMP;
		(void)snprintf(tmpfn, sizeof(tmpfn),
		    "%s/sdiff.XXXXXXXX", tmpdir);
		if ((tmpfd = mkstemp(tmpfn)) == -1)
			err(2, "%s", tmpfn);
		if ((tmpfp = fdopen(tmpfd, "w+")) == NULL)
			err(2, "fdopen");
	}

	addarg(&diffargs, &siz, argv[0]);
	addarg(&diffargs, &siz, argv[1]);
	addarg(&diffargs, &siz, NULL);

	if ((diffargs[0] = strrchr(diffprog, '/')) == NULL)
		diffargs[0] = diffprog;
	else
		diffargs[0]++;

	fd = startdiff(diffargs);
	free(diffargs);

	sdiff(fd);

	if (wait(&status) == -1)
		err(2, "wait");
	exit(WEXITSTATUS(status));
}

void
sdiff(int fd)
{
	struct lbuf lbd, lbl, lbr;
	char *st_l, *st_r, *cur_l;
	ptrdiff_t off_l, off_r;
	int lna, lnb, i, j, c;
	struct hunk h;
	FILE *fpd;

	if ((fpd = fdopen(fd, "r")) == NULL)
		err(2, "fdopen %s", diffprog);
	lna = lnb = 1;
	LBUF_INIT(lbd);
	LBUF_INIT(lbl);
	LBUF_INIT(lbr);
	while (appendline(fpd, &lbd)) {
		/* Pass through any number of common lines. */
		st_l = LBUF_GET(lbd);
		if (st_l[0] == '=' && st_l[1] == ' ') {
			LBUF_SET(lbd, st_l + 2);
			if (leftcol)
				disp(&lbd, NULL, '(');
			else if (!suppresscommon)
				disp(&lbd, &lbd, ' ');
			LBUF_SET(lbd, st_l);

			LBUF_CHOP(lbd);
			LBUF_APPEND(lbd, '\n');
			LBUF_APPEND(lbd, '\0');
			LBUF_SET(lbd, st_l + 2);
			if (outfp != NULL)
				save(outfp, &lbd);
			LBUF_SET(lbd, st_l);
			LBUF_RESET(lbd);
			lna++;
			lnb++;
			continue;
		}

		if (!readhunk(LBUF_GET(lbd), &h))
			errx(2, "malformed hunk header: %s",
			    LBUF_GET(lbd));
		if (h.h_type == HT_ADD)
			h.h_a++;
		else if (h.h_type == HT_DEL)
			h.h_c++;
		if (h.h_a != lna || h.h_c != lnb)
			errx(2, "bad hunk header: %s", LBUF_GET(lbd));

		/* Test for EOF. */
		if ((c = fgetc(fpd)) == EOF && h.h_type == HT_ADD)
			break;
		ungetc(c, fpd);
		switch (h.h_type) {
		case HT_ADD:
			for (; h.h_c <= h.h_d; h.h_c++, lnb++) {
				off_r = LBUF_LEN(lbr);

				if (fgetc(fpd) != '>' ||
				    fgetc(fpd) != ' ' ||
				    !appendline(fpd, &lbr))
					errx(2, "malformed hunk (%s): %s",
					    LBUF_GET(lbd), LBUF_GET(lbr));

				st_r = LBUF_GET(lbr);
				LBUF_SET(lbr, st_r + off_r);
				disp(NULL, &lbr, '>');
				LBUF_SET(lbr, st_r);

				LBUF_CHOP(lbr);
				LBUF_APPEND(lbr, '\n');
			}
			LBUF_APPEND(lbr, '\0');
			break;
		case HT_CHG:
			/* Can't seek on a pipe; read it all in. */
			for (i = 0; i <= h.h_b - h.h_a; i++) {
				if (fgetc(fpd) != '<' ||
				    fgetc(fpd) != ' ' ||
				    !appendline(fpd, &lbl))
					errx(2, "malformed hunk (%s): %s",
					    LBUF_GET(lbd), LBUF_GET(lbl));
				LBUF_CHOP(lbl);
				LBUF_APPEND(lbl, '\n');
			}
			LBUF_APPEND(lbl, '\0');

			st_l = cur_l = LBUF_GET(lbl);
			off_l = LBUF_LEN(lbl);

			/* Read the dummy `---' line. */
			if (fgetc(fpd) != '-' ||
			    fgetc(fpd) != '-' ||
			    fgetc(fpd) != '-' ||
			    fgetc(fpd) != '\n')
				errx(2, "malformed change hunk (%s)",
				    LBUF_GET(lbd));
			/* Print lines that were changed. */
			j = MIN(h.h_b - h.h_a, h.h_d - h.h_c);
			for (i = 0; i <= j;
			     i++, h.h_a++, h.h_c++, lna++, lnb++) {
				off_r = LBUF_LEN(lbr);

				/*
				 * By definition (i.e., MIN(), above),
				 * this _should_ return content.
				 */
				if (fgetc(fpd) != '>' ||
				    fgetc(fpd) != ' ' ||
				    !appendline(fpd, &lbr) || cur_l == NULL)
					errx(2, "malformed hunk (%s): %s",
					    LBUF_GET(lbd), LBUF_GET(lbr));

				LBUF_SET(lbl, cur_l);
				if ((cur_l = strchr(cur_l, '\n')) != NULL)
					*cur_l = '\0';

				st_r = LBUF_GET(lbr);
				LBUF_SET(lbr, st_r + off_r);
				disp(&lbl, &lbr, '|');
				LBUF_SET(lbr, st_r);

				if (cur_l)
					*cur_l++ = '\n';

				LBUF_CHOP(lbr);
				LBUF_APPEND(lbr, '\n');
			}
			/* Print lines that were deleted. */
			for (; h.h_a <= h.h_b; h.h_a++, lna++) {
				if (cur_l == NULL)
					errx(2, "short change hunk (%s)",
					    LBUF_GET(lbd));

				LBUF_SET(lbl, cur_l);
				if ((cur_l = strchr(cur_l, '\n')) != NULL)
					*cur_l = '\0';
				disp(&lbl, NULL, '<');
				if (cur_l)
					*cur_l++ = '\n';
			}
			LBUF_SET(lbl, st_l);
			/* Print lines that were added. */
			for (; h.h_c <= h.h_d; h.h_c++, lnb++) {
				off_r = LBUF_LEN(lbr);

				if (fgetc(fpd) != '>' ||
				    fgetc(fpd) != ' ' ||
				    !appendline(fpd, &lbr))
					errx(2, "malformed hunk (%s): %s",
					    LBUF_GET(lbd), LBUF_GET(lbr));

				st_r = LBUF_GET(lbr);
				LBUF_SET(lbr, st_r + off_r);
				disp(NULL, &lbr, '>');
				LBUF_SET(lbr, st_r);

				LBUF_CHOP(lbr);
				LBUF_APPEND(lbr, '\n');
			}
			LBUF_APPEND(lbr, '\0');
			break;
		case HT_DEL:
			for (; h.h_a <= h.h_b; h.h_a++, lna++) {
				off_l = LBUF_LEN(lbl);

				if (fgetc(fpd) != '<' ||
				    fgetc(fpd) != ' ' ||
				    !appendline(fpd, &lbl))
					errx(2, "malformed hunk (%s): %s",
					    LBUF_GET(lbd), LBUF_GET(lbl));

				st_l = LBUF_GET(lbl);
				LBUF_SET(lbl, st_l + off_l);
				disp(&lbl, NULL, '<');
				LBUF_SET(lbl, st_l);

				LBUF_CHOP(lbl);
				LBUF_APPEND(lbl, '\n');
			}
			LBUF_APPEND(lbl, '\0');
			break;
		}
		if (outfp != NULL)
			merge(outfp, &lbl, &lbr);
		LBUF_RESET(lbd);
		LBUF_RESET(lbl);
		LBUF_RESET(lbr);
	}
	/* Print remaining lines. */
	if (leftcol || !suppresscommon)
		for (;;) {
			if (!appendline(fpd, &lbl))
				break;
			if (leftcol)
				disp(&lbl, NULL, '(');
			else
				disp(&lbl, &lbl, ' ');
			LBUF_RESET(lbl);
		}
	(void)fclose(fpd);
	LBUF_FREE(lbd);
	LBUF_FREE(lbl);
	LBUF_FREE(lbr);

	if (outfp != NULL) {
		(void)fclose(outfp);
		(void)unlink(tmpfn);
	}
}

void
disp(struct lbuf *a, struct lbuf *b, char c)
{
	(void)printf("%-*.*s %c %-*.*s\n", width, width,
	    a == NULL ? "" : LBUF_GET(*a), c, width, width,
	    b == NULL ? "" : LBUF_GET(*b));
}

int
appendline(FILE *fp, struct lbuf *lb)
{
	int c, read;

	for (read = 0; (c = fgetc(fp)) != EOF && c != '\n'; read++)
		LBUF_APPEND(*lb, c);
	if (stripcr && lb->lb_buf[lb->lb_pos - 1] == '\r')
		LBUF_CHOP(*lb);
	LBUF_APPEND(*lb, '\0');
	return (read > 0);
}

void
merge(FILE *fp, struct lbuf *l, struct lbuf *r)
{
	static struct lbuf lb;
	static int alloc = 0;

	if (alloc)
		LBUF_RESET(lb);
	else
		LBUF_INIT(lb);
	ask(&lb, LBUF_GET(*l), LBUF_GET(*r));
	save(fp, &lb);
}

void
ask(struct lbuf *lb, const char *l, const char *r)
{
	char buf[10], *cmd;
	int done;

	done = 0;
	while (!done) {
		(void)printf("%s", prompt);
		(void)fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL) {
			if (feof(stdin)) {
				(void)printf("\n");
				fclose(outfp);
				exit(0);
			} else
				err(2, "fgets");
		}
		/* Trim trailing/beginning space. */
		for (cmd = buf + strlen(buf) - 1;
		    cmd > buf && isspace(*cmd); cmd--)
			*cmd = '\0';
		for (cmd = buf; isspace(*cmd); cmd++)
			;
		switch (*cmd) {
		case 'e':
			while (isspace(*++cmd))
				;
			done = 1;
			switch (*cmd) {
			case 'l':
				edit(lb, l, NULL);
				break;
			case 'b':
				edit(lb, l, r);
				break;
			case 'r':
				edit(lb, NULL, r);
				break;
			case '\0':
				edit(lb, NULL, NULL);
				break;
			default:
				done = 0;
				warnx("unknown edit modifier");
				break;
			}
			break;
		case 'l':
			for (; l != NULL && *l != '\0'; l++)
				LBUF_APPEND(*lb, *l);
			LBUF_APPEND(*lb, '\0');
			done = 1;
			break;
		case 'q':
			(void)fclose(outfp);
			exit(0);
			/* NOTREACHED */
		case 'r':
			for (; r != NULL && *r != '\0'; r++)
				LBUF_APPEND(*lb, *r);
			LBUF_APPEND(*lb, '\0');
			done = 1;
			break;
		case 's':
			suppresscommon = 1;
			break;
		case 'v':
			suppresscommon = 1;
			break;
		default:
			if (strcmp(cmd, "") != 0)
				(void)printf("%s: unknown command\n"
				    "Supported commands:\n"
				    "  e	Edit a blank line\n"
				    "  eb	Edit both displays\n"
				    "  el	Edit the left display\n"
				    "  er	Edit the right display\n"
				    "  l	Merge the left display\n"
				    "  q	Quit\n"
				    "  r	Merge the right display\n"
				    "  s	Enable silent mode\n"
				    "  v	Enable verbose mode\n", buf);
			break;
		}
#if 0
		if (*cmd != '\0' && *++cmd != '\0')
			warnx("junk at end of input");
#endif
	}
}

void
edit(struct lbuf *lb, const char *l, const char *r)
{
	static char *cmd = NULL;
	int status;

	if (cmd == NULL) {
		const char *editor, *s;
		size_t siz;
		char *p;

		if ((editor = getenv("EDITOR")) == NULL)
			editor = _PATH_VI;
		/*
		 * 1	argument spacing
		 * 2	quoting the editor
		 * 2	quoting the temporary filename
		 * 1	NUL-termination
		 * ===================================
		 * 6	total
		 */
		siz = strlen(editor) + 6 + strlen(tmpfn);
		for (s = editor; *s != '\0'; s++)
			if (strpbrk(s, "\\'"))
				siz++;
		for (s = tmpfn; *s != '\0'; s++)
			if (strpbrk(s, "\\'"))
				siz++;
		if ((cmd = malloc(siz)) == NULL)
			err(2, NULL);
		p = cmd;
		*p++ = '\'';
		for (s = editor; *s != '\0'; s++, p++) {
			if (strchr("\\'", *s) != NULL)
				*p++ = '\\';
			*p = *s;
		}
		*p++ = '\'';
		*p++ = ' ';
		*p++ = '\'';
		for (s = tmpfn; *s != '\0'; s++, p++) {
			if (strchr("\\'", *s) != NULL)
				*p++ = '\\';
			*p = *s;
		}
		*p++ = '\'';
		*p = '\0';
	}

	if (ftruncate(fileno(tmpfp), 0) == -1)
		err(2, "ftruncate");
	fpurge(tmpfp);
	if (l != NULL)
		if (fwrite(l, 1, strlen(l), tmpfp) != strlen(l))
			err(2, "fwrite");
	if (l != NULL && r != NULL)
		if (fwrite("\n", 1, 1, tmpfp) != 1)
			err(2, "fwrite");
	if (r != NULL)
		if (fwrite(r, 1, strlen(r), tmpfp) != strlen(r))
			err(2, "fwrite");
	status = system(cmd);
	if (!WIFEXITED(status))
		err(2, "%s", cmd);
//	if ()
//	LBUF_CAT();
}

#define ST_BEG  1	/* beginning */
#define ST_NUM1 2	/* after number 1 */
#define ST_COM1 3	/* after comma 1 */
#define ST_NUM2 4	/* after number 2 */
#define ST_TYPE 5	/* after type */
#define ST_NUM3 6	/* after number 3 */
#define ST_COM2 7	/* after comma 2 */
#define ST_NUM4 8	/* after number 4 */

int
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
		if (isdigit(*p)) {
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
				goto check;
			default:
				return (0);
			}
			p--;
			continue;
		}
		switch (*p) {
		case 'a':
		case 'd':
		case 'c':
			/*
			 * These can skip ST_COM1 and ST_NUM2.
			 * Fine-grained checking below.
			 */
			if (state != ST_NUM1 &&
			    state != ST_NUM2)
				return (0);
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
				return (0);
			if (state == ST_NUM1)
				state = ST_COM1;
			else
				state = ST_COM2;
			break;
		case '\n':
			goto check;
		default:
			return (0);
		}
	}

check:
	if (state != ST_NUM3 &&
	    state != ST_NUM4)
		return (0);
	if (*p != '\0')
		return (0);
	/* Fill in disambiguous values and sanity check. */
	switch (h->h_type) {
	case HT_ADD:
		if (h->h_b)
			return (0);
		/* XXaYY[,ZZ] */
		if (h->h_d) {
			if (h->h_c > h->h_d)
				return (0);
		} else
			h->h_d = h->h_c;
		break;
	case HT_DEL:
		if (h->h_d)
			return (0);
		/* XX[,YY]dZZ */
		if (h->h_b) {
			if (h->h_a > h->h_b)
				return (0);
		} else
			h->h_b = h->h_a;
		break;
	case HT_CHG:
		/* XX[,YY]cZZ[,QQ] */
		if (!h->h_b)
			h->h_b = h->h_a;
		if (!h->h_d)
			h->h_d = h->h_c;
		if (h->h_a > h->h_b ||
		    h->h_c > h->h_d)
			return (0);
		break;
	}
	return (1);
}

void
save(FILE *fp, struct lbuf *lb)
{
	if (fprintf(fp, "%s", LBUF_GET(*lb)) == -1)
		err(2, "fprintf");
}

int
startdiff(char **argv)
{
	int fd[2];

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
		(void)execv(diffprog, argv);
		err(2, "execve");
		/* NOTREACHED */
	}
	(void)close(fd[1]);
//	(void)close(STDIN_FILENO);
	return (fd[0]);
}

void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s [-abdilstW] [-I pattern] "
		"[-o file] [-w width] file1 file2\n", __progname);
	exit(2);
}
