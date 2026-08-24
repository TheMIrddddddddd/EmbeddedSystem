# Makefile - PC unit tests for M2 common components (WSL / Linux gcc)
#
# Usage (from WSL, project root is /mnt/d/.../IndustrialEmbedded):
#   cd test && make test
#
# Build: components under test + Unity + test cases -> single binary,
# run it, report pass/fail counts. No MCU hardware involved.

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -I../Common/inc -I../Middleware/Ringbuffer/inc -I../Middleware/Protocol/inc -I../Middleware/CLI/inc -I../Middleware/FlashKV/inc -Iunity -g -O0
TARGET  := m2_test

SRCS := \
    ../Common/src/common_crc.c \
    ../Middleware/Ringbuffer/src/ringbuffer.c \
    ../Middleware/Protocol/src/protocol_frame.c \
    ../Middleware/Protocol/src/modbus_rtu.c \
    ../Middleware/CLI/src/cli.c \
    ../Middleware/FlashKV/src/flash_kv.c \
    ../Common/src/upgrade_serialization.c \
    unity/unity.c \
    test_main.c \
    test_crc.c \
    test_ringbuffer.c \
    test_protocol_frame.c \
    test_modbus_rtu.c \
    test_cli.c \
    test_flash_kv.c \
    test_upgrade_serialization.c

all: test

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all test clean
