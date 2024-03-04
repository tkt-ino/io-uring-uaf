TARGET = uaf
OBJS   = main.o
CC     = gcc
FLAGS  = -luring

.PHONY: all clean dist-clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $^ $(FLAGS)

clean:
	$(RM) *.o

dist-clean: clean
	$(RM) $(TARGET)
