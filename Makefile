# $Id$

PROG = sdiff
SRCS = sdiff.c bufchunk.c
MAN = sdiff.1
CFLAGS = -Wall -g -I../lbuf

.include <bsd.prog.mk>
