/* $Id$ */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "lbuf.h"

void
lbuf_init(struct lbuf *lb)
{
	lb->pos = lb->max = -1;
	lb->buf = NULL;
}

void
lbuf_append(struct lbuf *lb, char ch)
{
	if (++lb->pos >= lb->max) {
		lb->max += 30;
		if ((lb->buf = realloc(lb->buf, lb->max)) == NULL)
			err(1, "lbuf_append");
	}
	lb->buf[lb->pos] = ch;
}

char *
lbuf_get(struct lbuf *lb)
{
	return (lb->buf);
}

void
lbuf_free(struct lbuf *lb)
{
	free(lb->buf);
}

void
lbuf_reset(struct lbuf *lb)
{
	lb->pos = -1;
}
