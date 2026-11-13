CC ?= gcc
CFLAGS ?= -Wall -Wextra -O3 -std=c99
override CFLAGS += $(CDEFS)
# LDFLAGS ?= -s

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

SRCPATHS = $(appprefix $(SRCDIR)/, $(SOURCES))
OBJPATHS = $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))
DEPPATHS = $(addprefix $(DEPDIR)/, $(SOURCES:.c=.d))
TARGET = $(BINDIR)/microb

.PHONY: all dirs mcudb
all: dirs $(TARGET) mcudb

dirs:
	@mkdir -p $(BINDIR) $(BUILDDIR) $(OBJDIR) $(DEPDIR)


.PHONY: $(TARGET)
$(TARGET): $(OBJPATHS)
	$(CC) -o $@ $^ $(LDFLAGS)

mcudb:
	@rm -rf $(BINDIR)/mcu
	cp -R mcu $(BINDIR)/mcu


$(OBJDIR)%.o: $(SRCDIR)%.c
	$(CC) $(CFLAGS) -MMD -MF $(DEPDIR)/$*.d -c $< -o $@

-include $(DEPPATHS)

.PHONY: clean
clean:
	rm -rf $(OBJPATHS) $(DEPPATHS) $(TARGET) $(BINDIR)/mcu
