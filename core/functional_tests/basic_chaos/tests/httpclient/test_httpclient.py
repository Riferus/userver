import asyncio

import pytest

DP_TIMEOUT_MS = 'X-YaTaxi-Client-TimeoutMs'
DP_DEADLINE_EXPIRED = 'X-YaTaxi-Deadline-Expired'


@pytest.fixture
def call(service_client, for_client_gate_port, gate):
    async def _call(htype='common', headers=None, **args):
        return await service_client.get(
            '/chaos/httpclient',
            params={'type': htype, 'port': str(for_client_gate_port), **args},
            headers=headers or {},
        )

    return _call


@pytest.fixture
def ok_mock(mockserver):
    @mockserver.handler('/test')
    async def mock(_request):
        return mockserver.make_response('OK!')

    return mock


async def test_ok(call, ok_mock):
    response = await call()
    assert response.status == 200
    assert response.text == 'OK!'


async def test_stop_accepting(call, gate, ok_mock):
    response = await call()
    assert response.status == 200
    assert gate.connections_count() >= 1

    await gate.stop_accepting()
    await gate.sockets_close()  # close keepalive connections
    assert gate.connections_count() == 0

    response = await call()
    assert response.status == 500
    assert gate.connections_count() == 0

    gate.start_accepting()

    response = await call()
    assert response.status == 200
    assert gate.connections_count() >= 1


async def test_close_after_headers(call, gate, mockserver):
    @mockserver.handler('/test')
    async def _mock(request):
        if on:
            await gate.sockets_close()
        return mockserver.make_response('OK!')

    on = True
    response = await call()
    assert response.status == 500

    on = False
    response = await call()
    assert response.status == 200


async def test_required_headers(call, gate, ok_mock):
    required_headers = ['X-YaRequestId', 'X-YaSpanId', 'X-YaTraceId']

    response = await call()
    assert response.status == 200
    assert all(key in response.headers for key in required_headers)

    await gate.stop_accepting()
    await gate.sockets_close()  # close keepalive connections
    assert gate.connections_count() == 0

    response = await call()
    assert response.status == 500
    assert all(key in response.headers for key in required_headers)
    assert gate.connections_count() == 0


@pytest.fixture
async def client_metrics(service_client, monitor_client, gate):
    # Give metrics and logs from the previous tests some time
    # to be written out asynchronously.
    await asyncio.sleep(0.1)
    # Avoid 'prepare' being accounted in the client metrics diff.
    await service_client.update_server_state()
    return monitor_client.metrics_diff(prefix='httpclient', diff_gauge=True)


@pytest.fixture
def slow_mock(mockserver):
    @mockserver.handler('/test')
    async def mock(request):
        assert DP_TIMEOUT_MS in request.headers
        await asyncio.sleep(0.3)  # 300ms
        return mockserver.make_response('OK!')

    return mock


@pytest.mark.parametrize('timeout,deadline', [(400, 500), (500, 400)])
async def test_deadline_ok(call, client_metrics, slow_mock, timeout, deadline):
    async with client_metrics:
        response = await call(
            headers={DP_TIMEOUT_MS: str(deadline)}, timeout=timeout,
        )
        assert response.status == 200
        assert response.text == 'OK!'

    assert client_metrics.value_at('cancelled-by-deadline', {}) == 0
    assert client_metrics.value_at('errors', {'http_error': 'ok'}) == 1
    assert client_metrics.value_at('errors', {'http_error': 'timeout'}) == 0


def get_handler_exception_logs(capture):
    return [
        log
        for log in capture.select()
        if log['text'].startswith('exception in \'handler-chaos-httpclient\'')
    ]


@pytest.mark.parametrize(
    'timeout,deadline,attempts', [(100, 400, 1), (100, 200, 1), (200, 500, 2)],
)
async def test_timeout_expired(
        service_client,
        call,
        client_metrics,
        slow_mock,
        timeout,
        deadline,
        attempts,
):
    async with service_client.capture_logs() as capture:
        async with client_metrics:
            response = await call(
                headers={DP_TIMEOUT_MS: str(deadline)},
                timeout=timeout,
                attempts=attempts,
            )
            assert response.status == 500
            assert response.text == ''

    assert client_metrics.value_at('cancelled-by-deadline', {}) == 0
    assert client_metrics.value_at('errors', {'http_error': 'ok'}) == 0
    # Client metrics are counted per attempt.
    assert (
        client_metrics.value_at('errors', {'http_error': 'timeout'})
        == attempts
    )

    logs = capture.select(stopwatch_name='external')
    assert len(logs) == 1
    assert logs[0]['error'] == '1'
    assert logs[0]['attempts'] == str(attempts)
    assert logs[0]['max_attempts'] == str(attempts)
    assert logs[0].get('cancelled_by_deadline', '0') == '0'
    assert logs[0]['error_msg'] == 'Timeout was reached'
    assert logs[0]['timeout_ms'] == str(timeout)
    assert 'propagated_timeout_ms' not in logs[0]

    logs = get_handler_exception_logs(capture)
    assert len(logs) == 1
    assert 'clients::http::TimeoutException' in logs[0]['text']


@pytest.mark.parametrize(
    'timeout,deadline,attempts',
    [
        (400, 100, 1),
        (200, 100, 1),
        # Note: together with exponential backoff, 3rd attempt will start
        # at around 500ms from the complete request start.
        (200, 600, 3),
        # Deadline will most of the time be reached between 1st and
        # 2nd attempts during the exponential backoff delay. If the service
        # lags, deadline will be reached during the 2nd attempt.
        (200, 215, 2),
    ],
)
async def test_deadline_expired(
        service_client,
        call,
        client_metrics,
        slow_mock,
        timeout,
        deadline,
        attempts,
):
    async with service_client.capture_logs() as capture:
        async with client_metrics:
            response = await call(
                headers={DP_TIMEOUT_MS: str(deadline)},
                timeout=timeout,
                attempts=attempts,
            )
            assert response.status == 498
            assert response.text == 'Deadline expired'

            # If deadline fires before timeout, metrics and logs will only
            # be written upon hitting timeout.
            await asyncio.sleep(timeout / 1000 + 0.1)

    # There might have been >1 attempts, but only 1 of them was cancelled.
    assert client_metrics.value_at('cancelled-by-deadline', {}) == 1
    assert client_metrics.value_at('errors', {'http_error': 'ok'}) == 0

    timed_out = client_metrics.value_at('errors', {'http_error': 'timeout'})
    if deadline == 215:
        # See the note on the corresponding 'deadline' parameter above.
        assert 1 <= timed_out <= 2
    else:
        # Client metrics are counted per attempt.
        assert timed_out == attempts

    logs = capture.select(stopwatch_name='external')
    assert len(logs) == 1
    assert logs[0]['error'] == '1'
    assert logs[0]['max_attempts'] == str(attempts)
    assert logs[0]['cancelled_by_deadline'] == '1'
    assert logs[0]['error_msg'] == 'Timeout was reached'
    assert logs[0]['timeout_ms'] == str(timeout)
    assert 0 <= int(logs[0]['propagated_timeout_ms']) <= deadline

    logs = get_handler_exception_logs(capture)
    assert len(logs) == 1
    assert 'clients::http::CancelException' in logs[0]['text']


@pytest.fixture
def fake_deadline_expired_mock(mockserver):
    @mockserver.handler('/test')
    async def mock(request):
        assert DP_TIMEOUT_MS in request.headers
        # We've estimated the time, which is required to handle the request,
        # against the passed deadline and decided that we definitely will not
        # be able to respond in time.
        return mockserver.make_response(
            status=504, headers={DP_DEADLINE_EXPIRED: '1'},
        )

    return mock


async def test_fake_deadline_expired(
        service_client, call, client_metrics, fake_deadline_expired_mock,
):
    async with service_client.capture_logs() as capture:
        async with client_metrics:
            response = await call(
                headers={DP_TIMEOUT_MS: '300'}, timeout=500, attempts=3,
            )
            assert response.status == 498
            assert response.text == 'Deadline expired'

    assert fake_deadline_expired_mock.times_called == 1

    assert client_metrics.value_at('cancelled-by-deadline', {}) == 1
    assert client_metrics.value_at('errors', {'http_error': 'ok'}) == 0
    assert client_metrics.value_at('errors', {'http_error': 'timeout'}) == 1

    logs = capture.select(stopwatch_name='external')
    assert len(logs) == 1
    assert logs[0]['error'] == '1'
    assert logs[0]['attempts'] == '1'
    assert logs[0]['max_attempts'] == '3'
    assert logs[0]['cancelled_by_deadline'] == '1'
    assert logs[0]['error_msg'] == 'Timeout was reached'
    assert logs[0]['timeout_ms'] == '500'
    assert 200 <= int(logs[0]['propagated_timeout_ms']) <= 300

    logs = get_handler_exception_logs(capture)
    assert len(logs) == 1
    assert 'clients::http::CancelException' in logs[0]['text']


@pytest.mark.parametrize(
    'retry_network_errors,retries_performed', [(True, 3), (False, 1)],
)
async def test_dp_timeout_not_retried(
        call,
        fake_deadline_expired_mock,
        retry_network_errors,
        retries_performed,
):
    # If the called service (fake_deadline_expired_mock in this case) returns
    # DP_DEADLINE_EXPIRED header, and our service did not spend the rest of its
    # deadline on the request, then userver HTTP client should retry
    # the request if and only if it should retry a timeout.
    response = await call(
        headers={DP_TIMEOUT_MS: '500'},
        timeout=100,
        attempts=3,
        retry_network_errors=int(retry_network_errors),
    )
    assert response.status == 500
    assert response.text == ''

    assert fake_deadline_expired_mock.times_called == retries_performed
