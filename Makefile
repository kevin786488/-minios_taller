CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude -D_XOPEN_SOURCE=700
LDFLAGS = -pthread
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)

minios: $(OBJ)
	$(CC) $(OBJ) -o minios $(LDFLAGS)

build/%.o: src/%.c include/*.h
	@mkdir -p build datos disk
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build minios

.PHONY: clean
