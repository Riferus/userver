import pytest

# /// [psql prepare]
from testsuite.databases.pgsql import discover

pytest_plugins = [
    'pytest_userver.plugins.samples',
    'pytest_userver.plugins.pgsql',
]


@pytest.fixture(scope='session')
def pgsql_local(service_source_dir, pgsql_local_create):
    databases = discover.find_schemas(
        'admin', [service_source_dir.joinpath('schemas/postgresql')],
    )
    return pgsql_local_create(list(databases.values()))
    # /// [psql prepare]
