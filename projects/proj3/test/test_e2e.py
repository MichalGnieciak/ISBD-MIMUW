import json
import os
import shutil
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
import pytest

import isbd_client
from isbd_client.api.metadata_api import MetadataApi
from isbd_client.api.schema_api import SchemaApi
from isbd_client.api.execution_api import ExecutionApi
from isbd_client.exceptions import ApiException
from isbd_client.models import (
    Column,
    ColumnExpression,
    ColumnReferenceExpression,
    CopyQuery,
    ExecuteQueryRequest,
    LogicalColumnType,
    QueryQueryDefinition,
    QueryStatus,
    SelectQuery,
    TableSchema,
)

BASE_URL = os.environ.get("ISBD_BASE_URL", "http://localhost:8080")
HOST_DATA_DIR = Path(__file__).resolve().parent.parent / "data"
DOCKER_SOCK = Path("/var/run/docker.sock")


@pytest.fixture(scope="module")
def api_clients():
    config = isbd_client.Configuration(host=BASE_URL)
    with isbd_client.ApiClient(config) as client:
        yield {
            "metadata": MetadataApi(client),
            "schema": SchemaApi(client),
            "execution": ExecutionApi(client),
        }


def wait_for_query(execution_api: ExecutionApi, query_id: str, timeout: float = 5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        q = execution_api.get_query_by_id(query_id=query_id)
        if q.status in (QueryStatus.COMPLETED, QueryStatus.FAILED):
            return q
        time.sleep(0.05)
    raise TimeoutError(f"Query {query_id} did not finish within {timeout}s")


def wait_for_query_raw(execution_api: ExecutionApi, query_id: str, timeout: float = 5.0) -> dict:
    """Polls GET /query/{queryId} as raw JSON to avoid OpenAPI Generator's ColumnExpression oneOf bug."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        resp = execution_api.get_query_by_id_without_preload_content(query_id=query_id)
        assert resp.status == 200
        raw = resp.data if resp.data else resp.read()
        payload = json.loads(raw.decode("utf-8"))
        if payload["status"] in ("COMPLETED", "FAILED"):
            return payload
        time.sleep(0.05)
    raise TimeoutError(f"Query {query_id} did not finish within {timeout}s")


def get_query_result_with_body(query_id: str, payload: dict | None = None) -> list[dict]:
    """Sends GET /result/{queryId} with a JSON body (OpenAPI Generator's Python rest.py drops GET bodies)."""
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    headers = {"Content-Type": "application/json"} if data is not None else {}
    req = urllib.request.Request(
        f"{BASE_URL}/result/{query_id}",
        data=data,
        headers=headers,
        method="GET",
    )
    try:
        with urllib.request.urlopen(req) as resp:
            assert resp.status == 200
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as err:
        raise ApiException(status=err.code, reason=err.reason, body=err.read().decode("utf-8"))


def _docker_unix_request(method: str, path: str) -> tuple[int, str]:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(DOCKER_SOCK))
        req = f"{method} {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
        sock.sendall(req.encode("utf-8"))
        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    raw = b"".join(chunks).decode("utf-8", errors="replace")
    header_part, _, body = raw.partition("\r\n\r\n")
    status_code = int(header_part.splitlines()[0].split()[1])
    return status_code, body


def restart_server_container(metadata_api: MetadataApi, timeout: float = 10.0) -> None:
    """Restarts the running server container via /var/run/docker.sock or the docker CLI."""
    target = os.environ.get("ISBD_CONTAINER_NAME")

    if DOCKER_SOCK.exists():
        if not target:
            status, body = _docker_unix_request("GET", "/containers/json")
            assert status == 200, f"Failed to list containers: {body}"
            for container in json.loads(body):
                names = [n.lstrip("/") for n in container.get("Names", [])]
                if "isbd-e2e" in names or "isbd-test" in names:
                    target = names[0]
                    break
                if any(p.get("PublicPort") == 8080 for p in container.get("Ports", [])):
                    target = names[0]
                    break
        if not target:
            pytest.skip("Could not identify running ISBD server container for restart test")

        status, body = _docker_unix_request("POST", f"/containers/{target}/restart?t=2")
        assert status in (204, 304), f"Docker restart failed ({status}): {body}"
    elif shutil.which("docker"):
        target = target or "isbd-e2e"
        subprocess.run(["docker", "restart", "-t", "2", target], check=True)
    else:
        pytest.skip("Mount /var/run/docker.sock or provide docker CLI to run restart persistence test")

    # Wait until the restarted server responds on /system/info
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            info = metadata_api.get_system_info()
            if info.interface_version == "2.1.0":
                return
        except Exception:
            time.sleep(0.1)
    raise TimeoutError(f"Server did not recover within {timeout}s after container restart")


def test_system_info(api_clients):
    info = api_clients["metadata"].get_system_info()
    assert info.interface_version == "2.1.0"
    assert info.uptime >= 0


def test_table_lifecycle_and_validation(api_clients):
    schema_api: SchemaApi = api_clients["schema"]

    # Create table
    table_id = schema_api.create_table(
        table_schema=TableSchema(
            name="users_test",
            columns=[
                Column(name="id", type=LogicalColumnType.INT64),
                Column(name="username", type=LogicalColumnType.VARCHAR),
            ],
        )
    )
    assert isinstance(table_id, str) and len(table_id) > 0

    # Duplicate table name -> 400
    with pytest.raises(ApiException) as exc:
        schema_api.create_table(
            table_schema=TableSchema(
                name="users_test",
                columns=[Column(name="id", type=LogicalColumnType.INT64)],
            )
        )
    assert exc.value.status == 400

    # List & inspect schema
    tables = schema_api.get_tables()
    assert any(t.table_id == table_id and t.name == "users_test" for t in tables)

    fetched = schema_api.get_table_by_id(table_id=table_id)
    assert fetched.name == "users_test"
    assert [c.name for c in fetched.columns] == ["id", "username"]

    # Delete & verify 404
    schema_api.delete_table(table_id=table_id)
    with pytest.raises(ApiException) as exc:
        schema_api.get_table_by_id(table_id=table_id)
    assert exc.value.status == 404


def test_copy_select_and_result_pagination(api_clients):
    schema_api: SchemaApi = api_clients["schema"]
    exec_api: ExecutionApi = api_clients["execution"]

    table_id = schema_api.create_table(
        table_schema=TableSchema(
            name="orders_test",
            columns=[
                Column(name="order_id", type=LogicalColumnType.INT64),
                Column(name="item", type=LogicalColumnType.VARCHAR),
            ],
        )
    )
    csv_path = HOST_DATA_DIR / "orders.csv"

    try:
        csv_path.write_text("order_id;item\n10;keyboard\n20;mouse\n30;monitor\n", encoding="utf-8")

        # 1. Submit COPY
        copy_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    CopyQuery(
                        source_filepath="/data/orders.csv",
                        destination_table_name="orders_test",
                        does_csv_contain_header=True,
                    )
                )
            )
        )
        copy_state = wait_for_query(exec_api, copy_id)
        if copy_state.status != QueryStatus.COMPLETED:
            err = exec_api.get_query_error(query_id=copy_id)
            pytest.fail(f"COPY failed with problems: {err.problems}")
        assert copy_state.is_result_available is False

        # 2. Submit SELECT
        select_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    SelectQuery(
                        column_clauses=[
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="orders_test", column_name="order_id"
                                )
                            ),
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="orders_test", column_name="item"
                                )
                            ),
                        ]
                    )
                )
            )
        )
        select_state = wait_for_query_raw(exec_api, select_id)
        if select_state["status"] != "COMPLETED":
            err = exec_api.get_query_error(query_id=select_id)
            pytest.fail(f"SELECT failed with problems: {err.problems}")
        assert select_state["isResultAvailable"] is True

        # 3. Fetch first 2 rows without flushing
        partial = get_query_result_with_body(
            select_id,
            {"rowLimit": 2, "flushResult": False},
        )
        assert sum(b["rowCount"] for b in partial) == 2

        # 4. Fetch all rows and flush
        full = get_query_result_with_body(
            select_id,
            {"flushResult": True},
        )
        assert sum(b["rowCount"] for b in full) == 3

        # 5. Subsequent fetch after flush -> 400
        with pytest.raises(ApiException) as exc:
            get_query_result_with_body(select_id)
        assert exc.value.status == 400
    finally:
        csv_path.unlink(missing_ok=True)
        schema_api.delete_table(table_id=table_id)


def test_failed_copy_validation_and_error_endpoint(api_clients):
    schema_api: SchemaApi = api_clients["schema"]
    exec_api: ExecutionApi = api_clients["execution"]

    table_id = schema_api.create_table(
        table_schema=TableSchema(
            name="fail_test",
            columns=[Column(name="num", type=LogicalColumnType.INT64)],
        )
    )
    bad_csv = HOST_DATA_DIR / "bad_data.csv"

    try:
        # 1. Non-existent CSV fails synchronously during planning -> 400
        with pytest.raises(ApiException) as exc:
            exec_api.submit_query(
                execute_query_request=ExecuteQueryRequest(
                    query_definition=QueryQueryDefinition(
                        CopyQuery(
                            source_filepath="/data/does_not_exist.csv",
                            destination_table_name="fail_test",
                            does_csv_contain_header=False,
                        )
                    )
                )
            )
        assert exc.value.status == 400

        # 2. Malformed CSV row passes planning (200), fails during execution -> GET /error/{id}
        bad_csv.write_text("not_an_int\n", encoding="utf-8")
        copy_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    CopyQuery(
                        source_filepath="/data/bad_data.csv",
                        destination_table_name="fail_test",
                        does_csv_contain_header=False,
                    )
                )
            )
        )
        state = wait_for_query(exec_api, copy_id)
        assert state.status == QueryStatus.FAILED

        err_report = exec_api.get_query_error(query_id=copy_id)
        assert len(err_report.problems) >= 1
    finally:
        bad_csv.unlink(missing_ok=True)
        schema_api.delete_table(table_id=table_id)


def test_columns_and_data_persistence_across_restart(api_clients):
    schema_api: SchemaApi = api_clients["schema"]
    exec_api: ExecutionApi = api_clients["execution"]
    meta_api: MetadataApi = api_clients["metadata"]

    sample_values = {
        "INT64": ("col_int64", ["-42", "0", "9001"], [-42, 0, 9001]),
        "VARCHAR": ("col_varchar", ["alpha", "beta", "gamma"], ["alpha", "beta", "gamma"]),
        "DOUBLE": ("col_double", ["1.5", "-2.25", "3.0"], [1.5, -2.25, 3.0]),
        "FLOAT64": ("col_float64", ["1.5", "-2.25", "3.0"], [1.5, -2.25, 3.0]),
        "BOOL": ("col_bool", ["true", "false", "true"], [True, False, True]),
        "BOOLEAN": ("col_boolean", ["true", "false", "true"], [True, False, True]),
        "DATE": ("col_date", ["2026-01-15", "2026-06-30", "2026-11-23"], ["2026-01-15", "2026-06-30", "2026-11-23"]),
    }

    columns: list[Column] = []
    csv_cols: list[list[str]] = []
    expected_cols: list[list[object]] = []

    for member in LogicalColumnType:
        col_name, raw_vals, typed_vals = sample_values[member.value]
        columns.append(Column(name=col_name, type=member))
        csv_cols.append(raw_vals)
        expected_cols.append(typed_vals)

    table_id = schema_api.create_table(
        table_schema=TableSchema(name="persist_columns_test", columns=columns)
    )
    csv_path = HOST_DATA_DIR / "persist_columns.csv"

    try:
        header = ";".join(c.name for c in columns)
        rows = [";".join(csv_cols[c][r] for c in range(len(columns))) for r in range(3)]
        csv_path.write_text("\n".join([header, *rows]) + "\n", encoding="utf-8")

        copy_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    CopyQuery(
                        source_filepath="/data/persist_columns.csv",
                        destination_table_name="persist_columns_test",
                        does_csv_contain_header=True,
                    )
                )
            )
        )
        copy_state = wait_for_query(exec_api, copy_id)
        if copy_state.status != QueryStatus.COMPLETED:
            err = exec_api.get_query_error(query_id=copy_id)
            pytest.fail(f"COPY failed before restart: {err.problems}")

        # Restart the server container to test Metastore + .isbd columnar persistence
        restart_server_container(meta_api)

        # 1. Verify catalog and column metadata survived restart intact
        tables = schema_api.get_tables()
        assert any(t.table_id == table_id and t.name == "persist_columns_test" for t in tables)

        recovered_schema = schema_api.get_table_by_id(table_id=table_id)
        assert recovered_schema.name == "persist_columns_test"
        assert [(c.name, c.type) for c in recovered_schema.columns] == [
            (c.name, c.type) for c in columns
        ]

        # 2. Verify all persisted columnar values can be scanned after restart
        select_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    SelectQuery(
                        column_clauses=[
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="persist_columns_test", column_name=c.name
                                )
                            )
                            for c in columns
                        ]
                    )
                )
            )
        )
        select_state = wait_for_query_raw(exec_api, select_id)
        if select_state["status"] != "COMPLETED":
            err = exec_api.get_query_error(query_id=select_id)
            pytest.fail(f"SELECT failed after restart: {err.problems}")

        batches = get_query_result_with_body(select_id, {"flushResult": True})
        assert sum(b["rowCount"] for b in batches) == 3

        recovered_cols: list[list[object]] = [[] for _ in columns]
        for batch in batches:
            for idx, col_values in enumerate(batch["columns"]):
                recovered_cols[idx].extend(col_values)

        assert recovered_cols == expected_cols
    finally:
        csv_path.unlink(missing_ok=True)
        schema_api.delete_table(table_id=table_id)

def test_large_multi_batch_csv_and_cross_batch_pagination(api_clients):
    schema_api: SchemaApi = api_clients["schema"]
    exec_api: ExecutionApi = api_clients["execution"]

    total_rows = 150_000  # Exceeds 2x Batch::BATCH_SIZE (65,536) -> spans at least 3 batches
    table_id = schema_api.create_table(
        table_schema=TableSchema(
            name="big_batch_test",
            columns=[
                Column(name="id", type=LogicalColumnType.INT64),
                Column(name="label", type=LogicalColumnType.VARCHAR),
            ],
        )
    )
    csv_path = HOST_DATA_DIR / "big_orders.csv"

    try:
        lines = ["id;label"] + [f"{i};row_{i}" for i in range(total_rows)]
        csv_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        copy_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    CopyQuery(
                        source_filepath="/data/big_orders.csv",
                        destination_table_name="big_batch_test",
                        does_csv_contain_header=True,
                    )
                )
            )
        )
        copy_state = wait_for_query(exec_api, copy_id, timeout=15.0)
        if copy_state.status != QueryStatus.COMPLETED:
            err = exec_api.get_query_error(query_id=copy_id)
            pytest.fail(f"Large COPY failed: {err.problems}")

        select_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    SelectQuery(
                        column_clauses=[
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="big_batch_test", column_name="id"
                                )
                            ),
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="big_batch_test", column_name="label"
                                )
                            ),
                        ]
                    )
                )
            )
        )
        select_state = wait_for_query_raw(exec_api, select_id, timeout=15.0)
        if select_state["status"] != "COMPLETED":
            err = exec_api.get_query_error(query_id=select_id)
            pytest.fail(f"Large SELECT failed: {err.problems}")

        # 1. Fetch all batches without flushing and verify multi-batch structure + values
        batches = get_query_result_with_body(select_id, {"flushResult": False})
        assert len(batches) >= 2, f"Expected multiple batches for {total_rows} rows, got {len(batches)}"
        assert sum(b["rowCount"] for b in batches) == total_rows

        all_ids: list[int] = []
        all_labels: list[str] = []
        for batch in batches:
            all_ids.extend(batch["columns"][0])
            all_labels.extend(batch["columns"][1])

        assert all_ids == list(range(total_rows))
        assert all_labels[0] == "row_0"
        assert all_labels[70_000] == "row_70000"
        assert all_labels[-1] == f"row_{total_rows - 1}"

        # 2. Slice across a batch boundary (full batch 0 + 5 rows from batch 1) and flush
        first_batch_size = batches[0]["rowCount"]
        cross_limit = first_batch_size + 5
        sliced = get_query_result_with_body(
            select_id, {"rowLimit": cross_limit, "flushResult": True}
        )
        assert len(sliced) == 2
        assert sliced[0]["rowCount"] == first_batch_size
        assert sliced[1]["rowCount"] == 5
        assert sum(b["rowCount"] for b in sliced) == cross_limit
    finally:
        csv_path.unlink(missing_ok=True)
        schema_api.delete_table(table_id=table_id)


def test_multi_part_table_appends_and_scans_all_parts(api_clients):
    schema_api: SchemaApi = api_clients["schema"]
    exec_api: ExecutionApi = api_clients["execution"]

    rows_per_part = 30_000  # 3 parts x 30,000 = 90,000 rows (also spans >1 batch)
    num_parts = 3

    table_id = schema_api.create_table(
        table_schema=TableSchema(
            name="multipart_test",
            columns=[
                Column(name="part_idx", type=LogicalColumnType.INT64),
                Column(name="seq", type=LogicalColumnType.INT64),
                Column(name="tag", type=LogicalColumnType.VARCHAR),
            ],
        )
    )
    csv_paths = [HOST_DATA_DIR / f"multipart_{p}.csv" for p in range(num_parts)]

    try:
        # 1. Run 3 separate COPY queries into the same table -> creates 3 part_<uid>.isbd files
        for p, csv_path in enumerate(csv_paths):
            lines = ["part_idx;seq;tag"] + [
                f"{p};{r};p{p}_r{r}" for r in range(rows_per_part)
            ]
            csv_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

            copy_id = exec_api.submit_query(
                execute_query_request=ExecuteQueryRequest(
                    query_definition=QueryQueryDefinition(
                        CopyQuery(
                            source_filepath=f"/data/{csv_path.name}",
                            destination_table_name="multipart_test",
                            does_csv_contain_header=True,
                        )
                    )
                )
            )
            copy_state = wait_for_query(exec_api, copy_id, timeout=15.0)
            if copy_state.status != QueryStatus.COMPLETED:
                err = exec_api.get_query_error(query_id=copy_id)
                pytest.fail(f"COPY part {p} failed: {err.problems}")

        # 2. Confirm 3 distinct part_*.isbd files exist on disk for this table
        table_dir = HOST_DATA_DIR / "db" / "tables" / table_id
        part_files = sorted(table_dir.glob("part_*.isbd"))
        assert len(part_files) == num_parts

        # 3. SELECT across all parts and verify total row count and ordering
        select_id = exec_api.submit_query(
            execute_query_request=ExecuteQueryRequest(
                query_definition=QueryQueryDefinition(
                    SelectQuery(
                        column_clauses=[
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="multipart_test", column_name="part_idx"
                                )
                            ),
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="multipart_test", column_name="seq"
                                )
                            ),
                            ColumnExpression(
                                ColumnReferenceExpression(
                                    table_name="multipart_test", column_name="tag"
                                )
                            ),
                        ]
                    )
                )
            )
        )
        select_state = wait_for_query_raw(exec_api, select_id, timeout=15.0)
        if select_state["status"] != "COMPLETED":
            err = exec_api.get_query_error(query_id=select_id)
            pytest.fail(f"Multi-part SELECT failed: {err.problems}")

        batches = get_query_result_with_body(select_id, {"flushResult": True})
        assert sum(b["rowCount"] for b in batches) == num_parts * rows_per_part

        part_col: list[int] = []
        seq_col: list[int] = []
        tag_col: list[str] = []
        for batch in batches:
            part_col.extend(batch["columns"][0])
            seq_col.extend(batch["columns"][1])
            tag_col.extend(batch["columns"][2])

        expected_parts = [p for p in range(num_parts) for _ in range(rows_per_part)]
        expected_seqs = [r for _ in range(num_parts) for r in range(rows_per_part)]
        assert part_col == expected_parts
        assert seq_col == expected_seqs
        assert tag_col[0] == "p0_r0"
        assert tag_col[rows_per_part] == "p1_r0"
        assert tag_col[-1] == f"p{num_parts - 1}_r{rows_per_part - 1}"
    finally:
        for csv_path in csv_paths:
            csv_path.unlink(missing_ok=True)
        schema_api.delete_table(table_id=table_id)