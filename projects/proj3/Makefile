.PHONY: all test docker test-docker clean

IMAGE_NAME ?= isbd-db
TEST_IMAGE_NAME ?= isbd-test

all:
	mkdir -p build
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$$(nproc)

test: all
	ctest --test-dir build --output-on-failure --verbose

docker:
	docker build -t $(IMAGE_NAME) -f Dockerfile ../..

test-docker:
	docker build --target builder -t $(TEST_IMAGE_NAME) -f Dockerfile ../..
	docker run --rm -t $(TEST_IMAGE_NAME) ctest --test-dir /app/build --output-on-failure --verbose

clean:
	rm -rf build