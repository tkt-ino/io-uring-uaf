TARGET   = uaf
OBJS     = dummy_check.o
CC       = gcc
FLAGS    = -luring
LIB      = $(shell pwd)/wpa/target/debug/libwpa.a
RUST_SRC = $(shell pwd)/wpa/src/*.rs

.PHONY: all clean distclean

all: $(TARGET)

$(TARGET): $(OBJS) $(LIB)
	$(CC) -o $(TARGET) $^ $(FLAGS)

$(LIB): $(RUST_SRC)
	cd wpa && cargo build

clean:
	$(RM) $(OBJS)

distclean: clean
	$(RM) $(TARGET)
