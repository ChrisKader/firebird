# Firebird IDA instant debugger helper.
# This script is intended to be used with IDA's -r command line switch.

import os

import ida_dbg
import ida_kernwin
import ida_nalt


def log(message):
    ida_kernwin.msg("[firebird] %s\n" % message)


def main():
    host = os.getenv("FIREBIRD_GDB_HOST", "127.0.0.1").strip()
    port_str = os.getenv("FIREBIRD_GDB_PORT", "3333").strip()
    try:
        port = int(port_str, 10)
    except ValueError:
        port = 3333

    input_path = os.getenv("FIREBIRD_INPUT", "").strip()
    if not input_path:
        input_path = ida_nalt.get_input_file_path() or ""
    if input_path and not os.path.exists(input_path):
        log("input file not found: %s" % input_path)
        return

    if not ida_dbg.load_debugger("gdb", True):
        log("failed to load GDB debugger")
        return
    ida_dbg.set_remote_debugger(host, "", port)

    result = ida_dbg.start_process(input_path, "", "")
    if result != 1:
        log("failed to start debugger (result=%d)" % result)
        return
    if not input_path:
        log("debugger started without binary (gdb %s:%d)" % (host, port))
        return
    log("debugger started (gdb %s:%d)" % (host, port))


if __name__ == "__main__":
    main()
