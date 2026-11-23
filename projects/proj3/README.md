# ISBD Lab 3 — Columnar DBMS Server & CSV Ingestion Engine

A multi-threaded, REST-based analytical Database Management System built in C++17 on top of the custom `.isbd` columnar storage engine from Lab 2. It provides a persistent catalog (**Metastore**), asynchronous query execution (**Planner** & **Executor**), streaming CSV ingestion (`COPY`), and full table scans (`SELECT`).

### REST API & OpenAPI Tooling
Implemented the 10 REST endpoints directly with `cpp-httplib` and `nlohmann::json` rather than using OpenAPI Generator's `cpp-httplib-server` target. Inspecting the generated C++ server for `dbmsInterface.yaml` revealed duplicate route controllers from multi-tagged endpoints (`Proj3` duplicating `Schema`, `Execution`, and `Metadata`), empty stubs for polymorphic `oneOf` schemas (`queryDefinition` and mixed-type `QueryResult` columns), and incorrect response signatures that dropped array wrappers on `GET /tables`, `GET /queries`, and `GET /result/{queryId}` as well as the `200 OK` success response on `DELETE /table/{tableId}`. OpenAPI Generator is instead used to generate a typed Python client from `../../interface/dbmsInterface.yaml` for end-to-end `pytest` testing.

## Quick Start with Docker (Recommended)

### 1. Build the Docker Image
Using the provided `Makefile` (from `projects/proj3`):
```bash
make docker
```
*(Or manually from `projects/proj3` on Windows/Linux:)*
```bash
docker build -t isbd-db -f Dockerfile ../..
```

### 2. Run the Container

#### Option A: Run Ephemerally (No Host Mount)
To simply start the server on port `8080` with container-local storage:
```bash
docker run --rm -p 8080:8080 isbd-db
```

#### Option B: Run with Persistent Storage & CSV Mount (Recommended)
Mount a host directory to `/data` inside the container. This directory serves two purposes:
- **Input CSVs:** Place CSV files here on your host machine and reference them in `COPY` queries as `/data/<filename>.csv` (semicolon `;` delimited).
- **Database Persistence:** The server stores `metastore.json` and all `.isbd` table files in `/data/db`, preserving database state across container restarts.

**Linux / macOS:**
```bash
mkdir -p ./data
docker run --rm -p 8080:8080 -v "$(pwd)/data:/data" isbd-db
```

**Windows (PowerShell):**
```powershell
mkdir -Force .\data
docker run --rm -p 8080:8080 -v "${PWD}\data:/data" isbd-db
```

### 3. Verify the Server is Running
```bash
curl http://localhost:8080/system/info
```
Expected response:
```json
{
  "author": "MichalGnieciak",
  "interfaceVersion": "2.1.0",
  "uptime": 3,
  "version": "1.0.0"
}
```

---

## Running Unit Tests

The project includes three CTest suites covering storage persistence, CSV ingestion, and asynchronous query execution:
- **`test_metastore`** — Schema validation, catalog persistence across restarts, RAII deferred file deletion during active queries, `.tmp` crash recovery, and CRC32 corruption detection.
- **`test_csv_parser`** — `destinationColumns` mapping validation, multi-batch streaming ingestion (`Batch::BATCH_SIZE`), Lab 2 `Deserializer` round-trips, and fail-fast error cleanup.
- **`test_query_engine`** — Synchronous `Planner` validation, asynchronous `COPY` → `SELECT` projection pipelines, empty table scans, snapshot isolation, and TOCTOU safety during concurrent `DELETE TABLE`.

### Inside Docker (No Local C++ Toolchain Required)
Using the provided `Makefile`:
```bash
make test-docker
```

*(Or manually from `projects/proj3` on Windows/Linux:)*
```bash
docker build --target builder -t isbd-test -f Dockerfile ../..
docker run --rm -t isbd-test ctest --test-dir /app/build --output-on-failure --verbose
```

### Natively on Linux
Using the provided `Makefile`:
```bash
make test
```

*(Or manually after `make all`:)*
```bash
ctest --test-dir build --output-on-failure --verbose
```

---

## Running End-to-End REST API Tests

`test/test_e2e.py` tests the running server over HTTP using a Python SDK generated from `../../interface/dbmsInterface.yaml`.

### Automated One-Step Run (`Makefile` / CMake)
Builds the Docker image, generates `generated_client/`, starts a temporary server container, runs `pytest test/test_e2e.py -v`, and removes `generated_client/` and the temporary container afterward (run from `projects/proj3`):
```bash
make test-e2e
```
*(Or via CMake: `cmake --build build --target test_e2e`)*

### Manual Run (Windows PowerShell from `projects/proj3`)
```powershell
# 1. Generate Python client from root/interface/dbmsInterface.yaml
docker run --rm -v "${PWD}/../..:/local" openapitools/openapi-generator-cli generate `
  -i /local/interface/dbmsInterface.yaml -g python -o /local/projects/proj3/generated_client `
  --model-name-mappings Literal=LiteralValue --additional-properties="packageName=isbd_client"

# 2. Start server & run pytest in a disposable container
mkdir -Force .\data
docker run -d --name isbd-e2e -p 8080:8080 -v "${PWD}\data:/data" isbd-db
docker run --rm --network container:isbd-e2e -v "${PWD}:/work" -w /work python:3.12-slim `
  sh -c "pip install -q -e ./generated_client pytest && pytest test/test_e2e.py -v"

# 3. Clean up
docker rm -f isbd-e2e
Remove-Item -Recurse -Force .\generated_client
```