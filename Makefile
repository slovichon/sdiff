# $Id$

PROG = sdiff
SRCS = sdiff.c
MAN = sdiff.1
CFLAGS = -I${.CURDIR}/../lbuf

.include <bsd.prog.mk>
