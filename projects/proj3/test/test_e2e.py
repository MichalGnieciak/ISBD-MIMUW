import json
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

BASE_URL = "http://localhost:8080"
HOST_DATA_DIR = Path(__file__).resolve().parent.parent / "data"


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