/* $Id$ */

struct lbuf {
	int pos, max;
	char *buf;
};

char *lbuf_get(struct lbuf *);
void  lbuf_append(struct lbuf *, char);
void  lbuf_free(struct lbuf *);
void  lbuf_init(struct lbuf *);
void  lbuf_reset(struct lbuf *);
