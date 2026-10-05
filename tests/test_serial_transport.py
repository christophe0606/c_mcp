"""Test default stdio hooks: uv run --no-project tests/test_serial_transport.py <driver>."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

SERVER = str(Path(sys.argv.pop(1)).resolve())


def wire(method, identifier, params=None):
    request = {"jsonrpc": "2.0", "id": identifier, "method": method}
    if params is not None:
        request["params"] = params
    return json.dumps(request).encode()


def exchange(data):
    process = subprocess.run([SERVER], input=data, capture_output=True, check=True, timeout=10)
    return [json.loads(line) for line in process.stdout.splitlines()]


class DefaultStdio(unittest.TestCase):
    def test_handshake_schema_echo_and_notification(self):
        reply = exchange(b"\r\n" + wire("initialize", 1) + b"\r\n" +
                         b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n' +
                         wire("tools/list", 2) + b"\r" +
                         wire("tools/call", 3, {"name": "echo", "arguments": {"text": "hello \"world\""}}))
        self.assertEqual([item["id"] for item in reply], [1, 2, 3])
        tools = {tool["name"]: tool for tool in reply[1]["result"]["tools"]}
        self.assertEqual(set(tools), {"echo", "add"})
        self.assertEqual(tools["echo"]["inputSchema"]["required"], ["text"])
        self.assertEqual(reply[2]["result"]["content"][0]["text"], 'hello "world"')

    def test_overflow_null_corruption_parser_error_and_recovery(self):
        reply = exchange(b"x" * 20000 + b"\n" + wire("ping", 99)[:10] + b"\0" +
                         wire("ping", 99)[10:] + b"\n{broken}\n" + wire("ping", 4) + b"\n")
        self.assertEqual([item["id"] for item in reply], [None, 4])
        self.assertEqual(reply[0]["error"]["code"], -32700)
        self.assertEqual(reply[1]["result"], {})

    def test_empty_eof_and_many_requests(self):
        self.assertEqual(exchange(b""), [])
        reply = exchange(b"\n".join(wire("ping", i) for i in range(100)))
        self.assertEqual([item["id"] for item in reply], list(range(100)))

    def test_numeric_callback_envelopes_trailing_input_and_notifications(self):
        # Preserve the former demo's stdio coverage in this test-only fixture.
        reply = exchange(b"\n".join([
            wire("initialize", 1),
            wire("tools/list", 2),
            wire("tools/call", 3, {"name": "add", "arguments": {"a": 2, "b": 3}}),
            b'{"jsonrpc":"1.0","id":4,"method":"ping"}',
            b"{broken",
            wire("ping", 5) + b" trailing",
            b'{"jsonrpc":"2.0","method":"tools/call",'
            b'"params":{"name":"echo","arguments":{"text":"silent"}}}',
            wire("ping", 6),
        ]) + b"\n")
        self.assertEqual([item["id"] for item in reply], [1, 2, 3, None, None, None, 6])
        self.assertEqual(reply[0]["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(reply[2]["result"]["content"][0]["text"], "5")
        self.assertEqual([item["error"]["code"] for item in reply[3:6]],
                         [-32600, -32700, -32700])
        self.assertEqual(reply[-1]["result"], {})


if __name__ == "__main__":
    unittest.main()
