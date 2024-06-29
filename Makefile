all: clean mypipeline looper myshell # valgrind

# LineParser: LineParser.o
# 	gcc -g -m32 -Wall -o LineParser LineParser.o

mypipeline: mypipeline.o 
	gcc -g -m32 -Wall -o mypipeline mypipeline.o 

mypipeline.o: mypipeline.c
	gcc -g -m32 -Wall -c -o mypipeline.o mypipeline.c

LineParser.o: LineParser.c LineParser.h
	gcc -g -m32 -Wall -c -o LineParser.o LineParser.c

looper: looper.o
	gcc -g -m32 -Wall -o looper looper.o

looper.o: looper.c
	gcc -g -m32 -Wall -c looper.c

myshell: myshell.o LineParser.o
	gcc -g -m32 -Wall -o myshell myshell.o LineParser.o

myshell.o: myshell.c
	gcc -g -m32 -Wall -c myshell.c

valgrind: myshell
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./myshell -d

.PHONY: clean

clean:
	rm -f *.o mypipeline looper myshell
