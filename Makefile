
PREFIX ?= /usr

all: wifi.c
	@gcc -o wifi wifi.c -liw -lncurses
	@strip wifi

install: all
	@install -Dm755 wifi ${DESTDIR}${PREFIX}/bin/wifi
	@install -Dm644 wifi.service ${DESTDIR}${PREFIX}/lib/systemd/system/wifi.service

clean:
	@rm wifi
