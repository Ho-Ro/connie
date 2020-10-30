# for debian packager
BIN=$(DESTDIR)/usr/bin
MAN=$(DESTDIR)/usr/share/man/man1


# JACK_SESSION=-DJACK_SESSION 

CFLAGS=$(JACK_SESSION) -Wall -std=c99 -O3 -fomit-frame-pointer -pipe
CFLAGS_SSE=-DCONNIE_SSE $(CFLAGS) -march=pentium3 -msse -mfpmath=sse -ffast-math 
CFLAGS_I386=-DCONNIE_I386 $(CFLAGS)

#TARGETS=connie_i386 connie_sse
TARGETS=connie
all: $(TARGETS) 

deb: all
	fakeroot debian/rules binary


connie: connie_main.o connie_ui.o reverb.o
	gcc $(LDFLAGS) -o $@ $^ -lm -ljack -lconfuse

connie_main.o: connie_main.c connie.h connie_ui.h reverb.h scales.h
	gcc -c $(CFLAGS) -o $@ $<

connie_ui.o: connie_ui.c connie.h connie_tg.h connie_ui.h
	gcc -c $(CFLAGS) -o $@ $<

reverb.o: reverb.c reverb.h
	gcc -c $(CFLAGS) -o $@ $<


connie_sse: connie_main_sse.o connie_ui_sse.o reverb_sse.o
	gcc $(LDFLAGS) -o $@ $^ -lm -ljack -lconfuse

connie_main_sse.o: connie_main.c connie.h connie_ui.h reverb.h scales.h
	gcc -c $(CFLAGS_SSE) -o $@ $<

connie_ui_sse.o: connie_ui.c connie.h connie_tg.h connie_ui.h
	gcc -c $(CFLAGS_SSE) -o $@ $<

reverb_sse.o: reverb.c reverb.h
	gcc -c $(CFLAGS_SSE) -o $@ $<



connie_i386: connie_main_i386.o connie_ui_i386.o reverb_i386.o
	gcc $(LDFLAGS) -o $@ $^ -lm -ljack -lconfuse

connie_main_i386.o: connie_main.c connie.h connie_ui.h reverb.h scales.h
	gcc -c $(CFLAGS_I386) -o $@ $<

connie_ui_i386.o: connie_ui.c connie.h connie_tg.h connie_ui.h
	gcc -c $(CFLAGS_I386) -o $@ $<

reverb_i386.o: reverb.c reverb.h
	gcc -c $(CFLAGS_I386) -o $@ $<


clean:
	rm -f *~ .*~ *.o

distclean: clean
	rm $(TARGETS)
	rm build-stamp configure-stamp

debclean:
	fakeroot debian/rules clean


install: $(TARGETS) connie connie.1
	install -s -p $(TARGETS) $(BIN)
	install -p connie $(BIN)
	install -p connie.1 $(MAN)

uninstall:
	cd $(BIN) && rm -f $(TARGETS) 
	rm -f $(MAN)/connie.1

tar: debclean
	sh MKtar.sh
