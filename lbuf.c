/* $Id$ */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "lbuf.h"

void
lbuf_init(struct lbuf *lb)
{
	lb->lb_pos = lb->lb_max = -1;
	lb->lb_buf = NULL;
}

void
lbuf_append(struct lbuf *lb, char ch)
{
	if (++lb->lb_pos >= lb->lb_max) {
		lb->lb_max += 30;
		if ((lb->lb_buf = realloc(lb->lb_buf, lb->lb_max *
		     sizeof(*lb->lb_buf))) == NULL)
			err(1, "lbuf_append");
	}
	lb->lb_buf[lb->lb_pos] = ch;
}

char *
lbuf_get(struct lbuf *lb)
{
	return (lb->lb_buf);
}

void
lbuf_free(struct lbuf *lb)
{
	free(lb->lb_buf);
}

void
lbuf_reset(struct lbuf *lb)
{
	lb->lb_pos = -1;
}
