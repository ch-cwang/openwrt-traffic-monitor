# Compiler settings - Can be overridden by cross-compilation toolchains (e.g., OpenWrt SDK)
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99
LDFLAGS ?= 
LIBS = -lpcap -lpthread

# Project settings
TARGET = traffic_monitor
SRC_DIR = src
OBJ_DIR = obj

# Sources and Objects
SRCS = $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/*/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Default target
all: $(TARGET)

# Link the target
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Install rule (typical for OpenWrt/Linux)
install: $(TARGET)
	install -d $(DESTDIR)/usr/sbin
	install -m 755 $(TARGET) $(DESTDIR)/usr/sbin/
	install -d $(DESTDIR)/www/net_trail
	install -d $(DESTDIR)/www/cgi-bin
	install -m 644 web/index.html $(DESTDIR)/www/net_trail/index.html
	if [ -d web/css ]; then cp -r web/css $(DESTDIR)/www/net_trail/; fi
	if [ -d web/js ]; then cp -r web/js $(DESTDIR)/www/net_trail/; fi
	install -m 755 cgi-bin/firewall_cmd.sh $(DESTDIR)/www/cgi-bin/firewall_cmd.sh

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean install
