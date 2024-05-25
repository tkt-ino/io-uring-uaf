CC       = gcc
FLAGS    = -l:liburing.a -lpthread 
RUST_DIR = wpa
RUST_LIB = $(RUST_DIR)/target/debug/libwpa.a
RUST_SRC = $(RUST_DIR)/src/*.rs

.PHONY: all clean distclean

all: KeyRecovery DummyCheck DirtyCred

KeyRecovery: key_recovery.o $(RUST_LIB)
	$(CC) -o $@ $^ $(FLAGS)

DummyCheck: dummy_check.o $(RUST_LIB)
	$(CC) -o $@ $^ 

DirtyCred: dirty_cred.o
	$(CC) -o $@ $^ $(FLAGS)

$(RUST_LIB): $(RUST_SRC)
	cd $(RUST_DIR) && cargo build

clean:
	$(RM) *.o

distclean: clean
	$(RM) KeyRecovery DummyCheck DirtyCred
	cd $(RUST_DIR) && cargo clean
