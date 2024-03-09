CC       = gcc
FLAGS    = -luring
LIB      = wpa/target/debug/libwpa.a
RUST_SRC = wpa/src/*.rs

.PHONY: all clean distclean

all: KeyRecovery DummyCheck

KeyRecovery: key_recovery.o $(LIB)
	$(CC) -o $@ $^ $(FLAGS)

DummyCheck: dummy_check.o $(LIB)
	$(CC) -o $@ $^ 

$(LIB): $(RUST_SRC)
	cd wpa && cargo build

clean:
	$(RM) *.o

distclean: clean
	$(RM) KeyRecovery DummyCheck
