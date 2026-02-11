# IDA Instant Debugging (Experimental)

`Tools -> Launch IDA` can launch IDA and pass a remote GDB target argument for Firebird.

## Status

- Experimental feature.
- Not covered by automated tests.

## Prerequisites

- GDB server enabled in Firebird settings.
- Valid IDA executable path.
- Optional input file for IDA (binary/database target).

## Behavior

- Firebird launches IDA with: `-rgdb@<host>:<port>`
- Host defaults to `127.0.0.1` unless overridden in settings (`ida_gdb_host`).
- Port comes from current Firebird GDB configuration.

## Limitations

- Firebird only validates process startup, not successful debugger handshake.
- IDA version-specific command-line differences are not auto-detected.
