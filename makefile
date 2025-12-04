# this will detect the OS and use the appropriate commands
ifeq ($(OS),Windows_NT)
    RM = del /Q
    EXE = .exe
else
    RM = rm -f
    EXE =
endif


CC = gcc
CFLAGS = -c -Wall
LFLAGS = -lm

TARGET = VMCacheSim$(EXE)
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) *.o
	$(RM) $(TARGET)

