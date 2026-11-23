.PHONY: all test docker test-docker generate-client test-e2e clean

IMAGE_NAME ?= isbd-db
TEST_IMAGE_NAME ?= isbd-test
E2E_CONTAINER_NAME ?= isbd-e2e-server
REPO_ROOT := $(abspath $(CURDIR)/../..)

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

generate-client:
	rm -rf generated_client
	docker run --rm \
		-v "$(REPO_ROOT):/local" \
		openapitools/openapi-generator-cli generate \
		-i /local/interface/dbmsInterface.yaml \
		-g python \
		-o /local/projects/proj3/generated_client \
		--model-name-mappings Literal=LiteralValue \
		--additional-properties="packageName=isbd_client"

test-e2e: docker generate-client
	@mkdir -p data
	@docker rm -f $(E2E_CONTAINER_NAME) >/dev/null 2>&1 || true
	@docker network create isbd-net >/dev/null 2>&1 || true
	@docker run -d --name $(E2E_CONTAINER_NAME) --network isbd-net -p 8080:8080 -v "$$(pwd)/data:/data" $(IMAGE_NAME) >/dev/null
	@sleep 1
	@set +e; \
	docker run --rm --network isbd-net \
		-e ISBD_BASE_URL=http://$(E2E_CONTAINER_NAME):8080 \
		-e ISBD_CONTAINER_NAME=$(E2E_CONTAINER_NAME) \
		-v /var/run/docker.sock:/var/run/docker.sock \
		-v "$$(pwd):/work" -w /work \
		python:3.12-slim \
		sh -c "pip install -q -e ./generated_client pytest && pytest test/test_e2e.py -v"; \
	TEST_EXIT=$$?; \
	docker rm -f $(E2E_CONTAINER_NAME) >/dev/null 2>&1 || true; \
	docker network rm isbd-net >/dev/null 2>&1 || true; \
	rm -rf generated_client .pytest_cache; \
	exit $$TEST_EXIT

clean:
	rm -rf build generated_client .pytest_cache