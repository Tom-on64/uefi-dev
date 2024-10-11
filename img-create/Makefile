.POSIX:
.PHONY: all clean

SRC = ./src
TARGET = writegpt

CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2

all: $(TARGET)
$(TARGET): $(SRC)/$(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

