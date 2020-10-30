# for debin packager
BIN=$(DESTDIR)/usr/bin
MAN=$(DESTDIR)/usr/share/man/man1


TARGETS=connie

all: $(TARGETS)

connie: connie.o connie_ui.o
	gcc $(LDFLAGS) -o connie connie.o connie_ui.o -ljack

connie.o: connie.c connie_ui.h
	gcc -c -Wall -std=c99 --fast-math -o connie.o connie.c

connie_ui.o: connie_ui.c connie.h connie_tg.h connie_ui.h
	gcc -DCONNIE -c -Wall -std=c99 --fast-math -o connie_ui.o connie_ui.c

clean:
	rm -f *~ *.o $(TARGETS)

distclean: clean
	rm build-stamp configure-stamp

install: $(TARGETS)
	install -s -p $(TARGETS) $(BIN)
	install -p connie.1 $(MAN)

uninstall:
	cd $(BIN) && rm -f $(TARGETS) 
	rm -f $(MAN)/connie.1
