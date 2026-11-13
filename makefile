CC ?= gcc
CFLAGS ?= -Wall -Wextra -O3 -std=c99
override CFLAGS += $(CDEFS)
# LDFLAGS ?= -s

PREFIX ?= /usr/local
DESTDIR ?=

SRCDIR = src
BUILDDIR = build
BINDIR = bin
OBJDIR = $(BUILDDIR)/obj
DEPDIR = $(BUILDDIR)/dep

SOURCES = \
	config.c \
	ini.c \
	logger.c \
	utils.c \
	mcu_db.c \
	template.c \
	generator_makefile.c \
	main.c

OBJPATHS = $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))
DEPPATHS = $(addprefix $(DEPDIR)/, $(SOURCES:.c=.d))
TARGET = $(BINDIR)/microb

.PHONY: all mcudb test install uninstall clean
all: $(TARGET) mcudb

$(BINDIR) $(OBJDIR) $(DEPDIR):
	@mkdir -p $@

$(TARGET): $(OBJPATHS) | $(BINDIR)
	$(CC) -o $@ $^ $(LDFLAGS)

mcudb: | $(BINDIR)
	@rm -rf $(BINDIR)/mcu
	cp -R mcu $(BINDIR)/mcu

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR) $(DEPDIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)/$*.d -c $< -o $@

-include $(DEPPATHS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/microb
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/microb
	rm -rf $(DESTDIR)$(PREFIX)/share/microb/mcu
	cp -R mcu $(DESTDIR)$(PREFIX)/share/microb/mcu

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/microb
	rm -rf $(DESTDIR)$(PREFIX)/share/microb

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(BINDIR)/mcu
