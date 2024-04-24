CC       = gcc
FLAGS    = -luring -lpthread
RUST_DIR = wpa
LIB      = $(RUST_DIR)/target/debug/libwpa.a
RUST_SRC = $(RUST_DIR)/src/*.rs

.PHONY: all clean distclean

all: KeyRecovery DummyCheck DirtyCred

KeyRecovery: key_recovery.o $(LIB)
	$(CC) -o $@ $^ $(FLAGS)

DummyCheck: dummy_check.o $(LIB)
	$(CC) -o $@ $^ 

DirtyCred: dirty_cred.o
	$(CC) -o $@ $^ $(FLAGS)

$(LIB): $(RUST_SRC)
	cd $(RUST_DIR) && cargo build

clean:
	$(RM) *.o

distclean: clean
	$(RM) KeyRecovery DummyCheck DirtyCred
	cd $(RUST_DIR) && cargo clean
