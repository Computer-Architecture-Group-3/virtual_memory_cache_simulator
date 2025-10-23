CC = gcc

VMCacheSim.exe: main.o
	$(CC) main.c -o VMCacheSim.exe

clean:
	rm *.o
	rm VMCacheSim.exe

