CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
LDLIBS += -lmosquitto -lpthread
TARGET := bin/linux/gateway
SOURCES := src/main.c src/server_client.c src/gateway_config.c
HEADERS := src/server_client.h src/gateway_config.h

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -o $@ $(SOURCES) $(LDLIBS)

run: $(TARGET)
	GATEWAY_CONFIG=config/gateway.conf ./$(TARGET)

clean:
	rm -f $(TARGET)
