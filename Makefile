#CC = gcc
CC = /home/bianxindong/AQ-120/host/bin/aarch64-buildroot-linux-gnu-gcc
CFLAGS = -Wall -Wextra -g
TARGET = dma_tool

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET)

.PHONY: all clean 