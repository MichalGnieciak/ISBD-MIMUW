CARGO = cargo
TARGET_DIR = target/release

.PHONY: all clean help

all:
	$(CARGO) build --release

clean:
	$(CARGO) clean

help:
	@echo "Available commands:"
	@echo "  make all   - Builds all program files (latency, throughput, throughput_mmap, pipeline)
	@echo "  make clean - Cleans target/ artifacts"