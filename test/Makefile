# Makefile - PC unit tests for M2 common components (WSL / Linux gcc)
#
# Usage (from WSL, project root is /mnt/d/.../IndustrialEmbedded):
#   cd test && make test
#
# Build: components under test + Unity + test cases -> single binary,
# run it, report pass/fail counts. No MCU hardware involved.

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -I../Common/inc -I../Middleware/Ringbuffer/inc -I../Middleware/Protocol/inc -Iunity -g -O0
TARGET  := m2_test

SRCS := \
    ../Common/src/common_crc.c \
    ../Middleware/Ringbuffer/src/ringbuffer.c \
    ../Middleware/Protocol/src/protocol_frame.c \
    unity/unity.c \
    test_main.c \
    test_crc.c \
    test_ringbuffer.c \
    test_protocol_frame.c

all: test

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all test clean
