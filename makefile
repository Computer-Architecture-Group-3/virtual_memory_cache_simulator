CC = gcc
CFLAGS = -c -Wall
LFLAGS = -lm

TARGET = VMCacheSim.exe
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o
	rm -f $(TARGET)

