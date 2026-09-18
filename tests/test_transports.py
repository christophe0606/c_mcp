"""Exercise the Linux/macOS demo transports without third-party packages."""
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request


def messages():
    return [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize"},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
         "params": {"name": "add", "arguments": {"a": 2, "b": 3}}},
        {"jsonrpc": "1.0", "id": 4, "method": "ping"},
        "{broken",
        '{"jsonrpc":"2.0","id":5,"method":"ping"} trailing',
        {"jsonrpc": "2.0", "method": "tools/call",
         "params": {"name": "echo", "arguments": {"text": "silent"}}},
        {"jsonrpc": "2.0", "id": 6, "method": "ping"},
    ]


def wire(message):
    return message if isinstance(message, str) else json.dumps(message)


def check(responses):
    assert [r["id"] for r in responses] == [1, 2, 3, None, None, None, 6]
    assert responses[0]["result"]["protocolVersion"] == "2025-06-18"
    assert {t["name"] for t in responses[1]["result"]["tools"]} == {"echo", "add"}
    assert responses[2]["result"]["content"][0]["text"] == "5"
    assert [r["error"]["code"] for r in responses[3:6]] == [-32600, -32700, -32700]
    assert responses[-1]["result"] == {}


def test_stdio(binary):
    result = subprocess.run([binary], input="".join(wire(m) + "\n" for m in messages()),
                            text=True, capture_output=True, check=True, timeout=10)
    check([json.loads(line) for line in result.stdout.splitlines()])


def test_http(binary):
    process = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while True:
            if process.poll() is not None:
                raise RuntimeError(process.stderr.read().decode())
            try:
                with urllib.request.urlopen("http://127.0.0.1:8100/health", timeout=1) as response:
                    assert response.status == 200
                break
            except (urllib.error.URLError, TimeoutError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        responses = []
        for message in messages():
            request = urllib.request.Request("http://127.0.0.1:8100/mcp",
                data=wire(message).encode(), headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=5) as response:
                if isinstance(message, dict) and "id" not in message:
                    assert response.status == 202
                else:
                    assert response.status == 200
                    responses.append(json.load(response))
        check(responses)
    finally:
        process.terminate()
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


if __name__ == "__main__":
    test_stdio(sys.argv[1])
    test_http(sys.argv[2])
    print("stdio and HTTP transport tests passed")
