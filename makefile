# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC=gcc
CFLAGS=-std=c11 -Wall -g -O -pthread
LDLIBS=-lm -lrt -pthread

# elenco degli eseguibili da creare
EXECS = archivio.out

# primo target: gli eseguibili sono precondizioni
# quindi verranno tutti creati
all: $(EXECS) 

archivio: archivio.o xerrori.o

archivio.o: archivio.c xerrori.h

xerrori.o: xerrori.c xerrori.h




