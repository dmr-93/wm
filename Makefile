CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=gnu99
LDFLAGS  = $(shell pkg-config --cflags --libs x11 cairo fontconfig) -lm

SRCDIR   = src
OBJDIR   = obj
SRCS     = main.c atoms.c draw.c window.c menu.c events.c
OBJS     = $(SRCS:%.c=$(OBJDIR)/%.o)
TARGET   = inferno_wm

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags x11 cairo fontconfig) -I$(SRCDIR) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET) inferno_wm.desktop
	sudo install -m 755 $(TARGET) /usr/local/bin/
	sudo mkdir -p /usr/share/xsessions/
	sudo install -m 644 inferno_wm.desktop /usr/share/xsessions/
