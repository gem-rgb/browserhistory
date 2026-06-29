# Brave History Access — C Edition
# Makefile for GCC / MinGW

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -std=c11
TARGET  = brave-history

# Source files
SRCS = brave_history.c         \
       history_db.c            \
       export_json.c           \
       export_pdf.c            \
       import_ext.c            \
       categorize.c            \
       platform.c              \
       url_parser.c            \
       csv_import.c            \
       html_bookmark_import.c  \
       session_tracker.c       \
       domain_trie.c           \
       config_parser.c         \
       vendor/sqlite3.c

# Object files
OBJS = $(SRCS:.c=.o)

# Platform-specific linker flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lshell32 -lole32
    TARGET := $(TARGET).exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LDFLAGS = -lpthread -ldl -lm
    else ifeq ($(UNAME_S),Darwin)
        LDFLAGS = -lpthread -lm
    else
        LDFLAGS = -lpthread -lm
    endif
endif

# Build rules
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Individual compilation (for faster rebuilds)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
