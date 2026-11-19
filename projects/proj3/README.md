# ISBD Lab 3 — Columnar DBMS Server & CSV Ingestion Engine

A multi-threaded, REST-based analytical Database Management System built in C++17 on top of the custom `.isbd` columnar storage engine from Lab 2. It provides a persistent catalog (**Metastore**), asynchronous query execution (**Planner** & **Executor**), streaming CSV ingestion (`COPY`), and full table scans (`SELECT`).

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
Mount a host directory to `/data` inside the container. This directory serves two purposes:
- **Input CSVs:** Place CSV files here on your host machine and reference them in `COPY` queries as `/data/<filename>.csv`.
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
