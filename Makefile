TARGET = uaf
OBJS   = main.o
CC     = gcc
FLAGS  = -luring

.PHONY: all clean distclean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $^ $(FLAGS)

clean:
	$(RM) $(OBJS)

distclean: clean
	$(RM) $(TARGET)
