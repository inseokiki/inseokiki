CC      = gcc
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDE = -Iinclude
SRCDIR  = src
OBJDIR  = build
TARGET  = ber_sim

SRCS = $(SRCDIR)/main.c $(SRCDIR)/qam.c $(SRCDIR)/awgn.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(OBJDIR) $(TARGET)
