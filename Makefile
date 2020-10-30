Connie: Connie.c
	gcc -Wall -std=c99 -o Connie `pkg-config --cflags --libs jack` Connie.c
	strip Connie

clean:
	rm -f *~
