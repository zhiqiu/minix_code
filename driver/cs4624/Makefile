# Makefile for the CS4281 driver
PROG=	cs4624
SRCS=	cs4624.c mixer.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+= ${LIBAUDIODRIVER} ${LIBCHARDRIVER} ${LIBSYS}
LDADD+= -laudiodriver -lchardriver -lsys

MAN=

.include <minix.service.mk>
