BIN=$(DESTDIR)/usr/bin
MAN=$(DESTDIR)/usr/share/man/man1

TARGET=connie

connie: connie.c
	gcc $(CFLAGS) $(LDFLAGS) -Wall -std=c99 --fast-math -o connie -ljack connie.c

clean:
	rm -f *~ connie

distclean: clean
	rm connie build-stamp configure-stamp

install: connie
	install -s -p connie $(BIN)
	install -p connie.1 $(MAN)

uninstall:
	rm -f $(BIN)/connie 
	rm -f $(MAN)/connie.1
