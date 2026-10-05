"""Test default stdio hooks: uv run --no-project tests/test_stdio_transport.py <driver>."""
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
        self.assertEqual(reply[1]["result"]["tools"][0]["inputSchema"]["required"], ["text"])
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


if __name__ == "__main__":
    unittest.main()
