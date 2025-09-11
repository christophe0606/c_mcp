CC = gcc

SRC := $(wildcard *.c)
OBJ := $(patsubst %.c,build/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

TARGET = ctest
DEPFLAGS := -MMD -MP

CFLAGS = -Wall -Wextra -O2 -g $(DEPFLAGS)

DEP := $(OBJ:.o=.d)


all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -pthread -o $(TARGET) $(OBJ)


build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@ -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(DEP)

.PHONY: clean

clean:
	$(RM) -r build $(TARGET)
