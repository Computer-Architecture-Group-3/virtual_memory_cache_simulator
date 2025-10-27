CC = gcc

VMCacheSim.exe: main.o
	$(CC) main.c -o VMCacheSim.exe -lm

clean:
	rm *.o
	rm VMCacheSim.exe

