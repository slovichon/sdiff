/* $Id$ */

struct bufchunk {
	char		*bc_buf;
	struct bufchunk	*bc_next;
};

void	 bc_push(struct bufchunk **, char *);
char	*bc_pop(struct bufchunk **)
