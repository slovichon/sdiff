# $Id$

PROG = sdiff
SRCS = sdiff.c lbuf.c
MAN = sdiff.1
CFLAGS = -Wall -g

.include <bsd.prog.mk>
