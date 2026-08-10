BIN    := docksmith
SRC    := src/docksmith.c
CC     ?= gcc
CFLAGS ?= -O2 -Wall

.PHONY: all setup clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Create ~/.docksmith and import the base image. Run once before the first build.
setup: $(BIN)
	./scripts/setup.sh

clean:
	rm -f $(BIN)
	rm -rf temp_fs runtime_fs filelist.txt hash.txt layer.tar
	rm -rf examples/demo-app/temp_fs examples/demo-app/runtime_fs
