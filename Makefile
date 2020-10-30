# for debian packager
BIN=$(DESTDIR)/usr/bin
MAN=$(DESTDIR)/usr/share/man/man1


CFLAGS=-Wall -std=c99 -O3 -march=pentium3 -msse -mfpmath=sse -ffast-math -fomit-frame-pointer -pipe

TARGETS=connie

all: $(TARGETS)


connie: connie.o connie_ui.o freeverb.o
	gcc $(LDFLAGS) -o connie connie.o connie_ui.o freeverb.o -ljack

connie.o: connie.c connie_ui.h freeverb.h
	gcc -c $(CFLAGS) -o connie.o connie.c

freeverb.o: freeverb.c freeverb.h
	gcc -c $(CFLAGS) -o freeverb.o freeverb.c

connie_ui.o: connie_ui.c connie.h connie_tg.h connie_ui.h
	gcc -DCONNIE -c $(CFLAGS) -o connie_ui.o connie_ui.c


clean:
	rm -f *~ .*~ *.o $(TARGETS)

distclean: clean
	rm build-stamp configure-stamp

install: $(TARGETS)
	install -s -p $(TARGETS) $(BIN)
	install -p connie.1 $(MAN)

uninstall:
	cd $(BIN) && rm -f $(TARGETS) 
	rm -f $(MAN)/connie.1
