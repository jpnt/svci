CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200112L
LDFLAGS =

BIN     = svci
OBJ     = svci.o util.o

all: $(BIN)

debug: CFLAGS = -std=c99 -Wall -Wextra -O0 -g -DDEBUG -D_POSIX_C_SOURCE=200112L
debug: clean $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

svci.o: svci.c svci.h log.h util.h
util.o: util.c util.h

clean:
	rm -f $(OBJ) $(BIN)

valgrind: $(BIN)
	valgrind --leak-check=full --track-origins=yes ./$(BIN) $(ARGS)

.PHONY: all clean debug valgrind

