/* $Id$ */

struct lbuf {
	int	 lb_pos;
	int	 lb_max;
	char	*lb_buf;
};

char *lbuf_get(struct lbuf *);
void  lbuf_append(struct lbuf *, char);
void  lbuf_free(struct lbuf *);
void  lbuf_init(struct lbuf *);
void  lbuf_reset(struct lbuf *);
