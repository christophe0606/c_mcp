CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
OBJ = main.o cJSON.o processing.o stdio.o
TARGET = ctest

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -pthread -o $(TARGET) $(OBJ)

main.o: main.c cJSON.h config.h
	$(CC) $(CFLAGS) -c main.c

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) -c cJSON.c

processing.o: processing.c  config.h
	$(CC) $(CFLAGS) -c processing.c

stdio.o: stdio.c config.h
	$(CC) $(CFLAGS) -c stdio.c

clean:
	rm -f $(OBJ) $(TARGET)