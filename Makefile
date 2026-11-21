CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c99 -O2 $(shell pkg-config --cflags luajit tree-sitter 2>/dev/null || echo "-I/usr/include/luajit-2.1")
LDFLAGS ?= $(shell pkg-config --libs luajit tree-sitter 2>/dev/null || echo "-lluajit-5.1 -ltree-sitter")

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = purr

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/purr

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(DATADIR)/runtime
	install -m 644 runtime/*.lua $(DESTDIR)$(DATADIR)/runtime/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DATADIR)

.PHONY: all clean install uninstall
