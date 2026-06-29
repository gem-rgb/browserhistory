# ViewBrowserHistory — C Edition
# Makefile for GCC / MinGW / Clang
#
# Build: make
# Clean: make clean
# Install: make install

CC      ?= gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
TARGET  = viewbrowserhistory

# Source files — organized by subsystem
SRCS_CORE = brave_history.c    \
            history_db.c       \
            platform.c         \
            logging.c

SRCS_PARSER = url_parser.c            \
              csv_import.c            \
              html_bookmark_import.c  \
              config_parser.c

SRCS_ANALYSIS = session_tracker.c  \
                domain_trie.c      \
                url_categorize.c   \
                search_engine.c    \
                categorize.c

SRCS_IMPORT = import_ext.c

SRCS_EXPORT = export_json.c        \
              export_pdf.c         \
              export_csv.c         \
              export_html_report.c \
              export_markdown.c

SRCS_BROWSER = browser_detect.c

SRCS_VENDOR = vendor/sqlite3.c

SRCS = $(SRCS_CORE)     \
       $(SRCS_PARSER)   \
       $(SRCS_ANALYSIS) \
       $(SRCS_IMPORT)   \
       $(SRCS_EXPORT)   \
       $(SRCS_BROWSER)  \
       $(SRCS_VENDOR)

# Object files
OBJS = $(SRCS:.c=.o)

# Platform-specific linker flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lshell32 -lole32
    TARGET := $(TARGET).exe
    CFLAGS += -DPLATFORM_WINDOWS
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LDFLAGS = -lpthread -ldl -lm
        CFLAGS += -DPLATFORM_LINUX
    else ifeq ($(UNAME_S),Darwin)
        LDFLAGS = -lpthread -lm
        CFLAGS += -DPLATFORM_MACOS
    else
        LDFLAGS = -lpthread -lm
    endif
endif

# Optional: Use system SQLite instead of vendored
ifeq ($(USE_SYSTEM_SQLITE),1)
    CFLAGS += -DUSE_SYSTEM_SQLITE $(shell pkg-config --cflags sqlite3 2>/dev/null)
    LDFLAGS += $(shell pkg-config --libs sqlite3 2>/dev/null)
    SRCS_VENDOR =
endif

# ── Build rules ──────────────────────────────────────────────────

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Individual compilation (for faster rebuilds)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Install ──────────────────────────────────────────────────────

PREFIX ?= /usr/local

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)/etc/viewbrowserhistory
	@echo "Installed $(TARGET) to $(DESTDIR)$(PREFIX)/bin/"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Uninstalled $(TARGET)"

# ── Utilities ────────────────────────────────────────────────────

clean:
	rm -f $(TARGET) $(OBJS)

# Count lines of code (excluding vendor)
loc:
	@echo "Lines of code (excluding vendor):"
	@wc -l *.c *.h 2>/dev/null | tail -1

.PHONY: all clean install uninstall loc
