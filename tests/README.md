# CMSIS-MCP host tests

The CMake build in this repository is for host testing. Embedded applications
use the CMSIS-Pack component and build through their CMSIS csolution.
These tests require no board or simulator.

Run the following commands from the **repository root**, rather than this
`tests` directory. Install CMake 3.20 or newer, a native C compiler and
Python 3.10 or newer. The HTTP bridge tests also need `uv` or the Python
packages listed in `tools/requirements-mcp-serial-bridge.txt`.

## Build and run the C tests

Test with the supplied serial input loop enabled:

```sh
cmake -S . -B build/host-on -DCMCP_BUILD_TESTS=ON -DCMCP_BUILD_SERIAL=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-on --config Debug --parallel
ctest --test-dir build/host-on -C Debug --output-on-failure
```

Also test with the library's input loop disabled:

```sh
cmake -S . -B build/host-off -DCMCP_BUILD_TESTS=ON -DCMCP_BUILD_SERIAL=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-off --config Debug --parallel
ctest --test-dir build/host-off -C Debug --output-on-failure
```

The two build directories keep the configurations separate. `--config Debug`
and `-C Debug` select the configuration for generators such as Visual Studio;
`CMAKE_BUILD_TYPE=Debug` selects it for single-configuration generators.
If CMake selects the wrong Python interpreter, add
`-DPython3_EXECUTABLE="<python-executable>"` to the configure command.

CTest runs these checks in both configurations:

| Test | Coverage |
| --- | --- |
| `cmcp_core` | Registration, discovery, argument validation, callbacks, resources and request-memory recovery |
| `cmcp_core_no_vfs` | Tools continue to work when resource support is disabled |
| `cmcp_serial_transport` | Partial input, line delimiters, oversized lines and input-loss recovery |
| `cmcp_serial_stdio` | Default stdio transport through a native executable and Python fixture |

The serial fixtures exercise the input loop independently, including when
`CMCP_BUILD_SERIAL=OFF` on the library under test. The resources-disabled
test is included automatically; `CMCP_ENABLE_VFS` controls resource support
for the other targets and defaults to `ON`.

## Run the HTTP bridge tests

With `uv`, the test script installs its inline dependencies automatically:

```sh
uv run --script tests/test_serial_bridge.py -v
```

Alternatively, use an existing Python environment:

```sh
python -m pip install -r tools/requirements-mcp-serial-bridge.txt
python tests/test_serial_bridge.py -v
```

The suite uses simulated serial devices and the real HTTP MCP bridge. It
checks shared client sessions, tool and resource forwarding, reconnection,
notifications and failure handling. It does not open a board's serial port.

## Continuous integration

`.github/workflows/portable.yml` runs on Ubuntu for each push and pull
request. Its CTest matrix covers the serial loop enabled and disabled;
a separate job runs the HTTP bridge tests with Python 3.12.
