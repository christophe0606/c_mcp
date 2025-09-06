CC = gcc
CFLAGS = -Wall -Wextra -O2
OBJ = main.o cJSON.o
TARGET = ctest

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

main.o: main.c cJSON.h
	$(CC) $(CFLAGS) -c main.c

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) -c cJSON.c

clean:
	rm -f $(OBJ) $(TARGET)