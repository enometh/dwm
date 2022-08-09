# dwm - dynamic window manager
# See LICENSE file for copyright and license details.

include ../config.mk

#[Thu Mar 10 10:59:28 2016 +0530]
#LDFLAGS += --oformat=elf64-x86-64 -m elf_x86_64
#CFLAGS += -I/a/usr/include

SRC = ../drw.c ../dwm.c ../util.c
OBJ = $(patsubst ../%.c, %.o, $(SRC))

CFLAGS += -m64
LDFLAGS += -m64 -L/usr/lib64

all: options dwm

options:
	@echo dwm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

%.o: ../%.c
	${CC} -c -o $@ ${CFLAGS} -I. -I.. $<

${OBJ}: config.h ../config.mk rules.fragment ../patch-madhu.c

config.h: ../config.def.h
	@echo creating $@ from config.def.h
	@cp ../config.def.h $@

dwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -fv dwm ${OBJ} dwm-${VERSION}.tar.gz

dist: clean
	mkdir -p dwm-${VERSION}
	cp -R LICENSE Makefile README config.def.h config.mk\
		dwm.1 drw.h util.h ${SRC} dwm.png transient.c dwm-${VERSION}
	tar -cf dwm-${VERSION}.tar dwm-${VERSION}
	gzip dwm-${VERSION}.tar
	rm -rf dwm-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f dwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < dwm.1 > ${DESTDIR}${MANPREFIX}/man1/dwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm\
		${DESTDIR}${MANPREFIX}/man1/dwm.1

.PHONY: all options clean dist install uninstall

rules.fragment:
	touch rules.fragment
