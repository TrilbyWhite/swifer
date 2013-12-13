
PREFIX ?= /usr
PROG	= swifer

all: ${PROG}.c
	@gcc -o ${PROG} ${PROG}.c -liw -lncurses
	@strip ${PROG}

install: all
	@install -Dm755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	@install -Dm644 ${PROG}.service ${DESTDIR}${PREFIX}/lib/systemd/system/${PROG}.service
	@install -Dm644 ${PROG}.conf ${DESTDIR}/etc/${PROG}.conf
	@install -d ${DESTDIR}${PREFIX}/share/${PROG}

clean:
	@rm ${PROG}
