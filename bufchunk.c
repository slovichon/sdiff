/* $Id$ */

#include "bufchunk.h"

struct bufchunk {
	char		*bc_buf;
	struct bufchunk	*bc_next;
};

void	 bc_push(struct bufchunk **, char *);
char	*bc_pop(struct bufchunk **)

void
bc_push(struct bufchunk **bc, char *buf)
{
	struct bufchunk *new;

	if ((new = malloc(sizeof(*new))) == NULL)
		err(2, "malloc");
	new->bc_buf = buf;
	new->bc_next = *bc;
	*bc = new;
}

char *
bc_pop(struct bufchunk **bc)
{
	struct bufchunk *next;
	char *buf;

	buf = (*bc)->bc_buf;
	next = (*bc)->bc_next;
	free(*bc);
	*bc = next;
	return (buf);
}
