
PREFIX ?= /usr

all: wifi.c
	@gcc -o wifi wifi.c -liw -lncurses
	@strip wifi

install: all
	@install -Dm755 wifi ${DESTDIR}${PREFIX}/bin/wifi
	@install -Dm600 wifi.conf ${DESTDIR}/etc/wifi.conf

clean:
	@rm wifi
