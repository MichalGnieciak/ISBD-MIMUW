.PHONY: all docker clean

IMAGE_NAME ?= isbd-db

all:
	mkdir -p build
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$$(nproc)

docker:
	docker build -t $(IMAGE_NAME) -f Dockerfile ../..

clean:
	rm -rf build