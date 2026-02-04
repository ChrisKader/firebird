# Firebird IDA plugin to load debugger-provided metadata.
# Install by copying this file into ~/.idapro/plugins/.
# Usage:
#   - Start a Firebird debug session (GDB remote).
#   - In IDA, run: Edit -> Plugins -> Firebird Debug Helper
#   - Or use Debugger -> Firebird -> Load memory map (from debugger).

import os
import xml.etree.ElementTree as ET

import idaapi
import ida_dbg
import ida_kernwin
import ida_nalt
import ida_segment


PLUGIN_NAME = "Firebird Debug Helper"
PLUGIN_HELP = "Load Firebird memory map from debugger or file."
FIREBIRD_MENU = "Debugger/Firebird/"
FIREBIRD_MENU_NAME = "firebird_menu"
DEFAULT_GDB_HOST = "127.0.0.1"
DEFAULT_GDB_PORT = 3333

_last_gdb_host = DEFAULT_GDB_HOST
_last_gdb_port = DEFAULT_GDB_PORT
_last_app_path = ""
_menu_path = FIREBIRD_MENU
_menu_created = False


def log(message):
    ida_kernwin.msg("[firebird] %s\n" % message)


def ensure_menu():
    global _menu_path, _menu_created
    if _menu_created:
        return
    if ida_kernwin.create_menu(FIREBIRD_MENU_NAME, "Firebird", "Debugger/"):
        _menu_path = "Debugger/Firebird/"
        _menu_created = True
        return
    if ida_kernwin.create_menu(FIREBIRD_MENU_NAME, "Firebird", "Edit/Plugins/"):
        _menu_path = "Edit/Plugins/Firebird/"
        _menu_created = True
        return
    _menu_path = "Edit/Plugins/"
    _menu_created = False


def normalize_seg_name(name):
    name = name.strip()
    if not name:
        name = "seg"
    out = []
    for ch in name:
        if ch.isalnum() or ch in "._":
            out.append(ch)
        else:
            out.append("_")
    return "".join(out)


def unique_seg_name(name):
    base = normalize_seg_name(name)
    if idaapi.get_segm_by_name(base) is None:
        return base
    for i in range(1, 1000):
        candidate = "%s_%d" % (base, i)
        if idaapi.get_segm_by_name(candidate) is None:
            return candidate
    return base


def parse_map_xml(xml_text):
    tree = ET.ElementTree(ET.fromstring(xml_text))
    root = tree.getroot()
    if root.tag == "target":
        mm = root.find("memory-map")
        if mm is None:
            raise RuntimeError("memory-map not found in target")
        root = mm
    if root.tag != "memory-map":
        raise RuntimeError("unexpected root element: %s" % root.tag)
    entries = []
    for mem in root.findall("memory"):
        start = mem.get("start")
        length = mem.get("length")
        if not start or not length:
            continue
        name = mem.get("name") or ("seg_%s" % start)
        mem_type = (mem.get("type") or "ram").lower()
        perm = mem.get("perm") or mem.get("perms")
        entries.append((int(start, 0), int(length, 0), name, mem_type, perm))
    return entries


def parse_map_text(text):
    lines = []
    for line in text.splitlines():
        line = line.strip()
        if line:
            lines.append(line)
    if not lines or not lines[0].startswith("FBMAP"):
        raise RuntimeError("missing FBMAP header")
    entries = []
    for line in lines[1:]:
        if line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        start = parse_map_number(parts[0])
        length = parse_map_number(parts[1])
        mem_type = parts[2].lower()
        perm = parts[3].lower()
        name = " ".join(parts[4:])
        entries.append((start, length, name, mem_type, perm))
    return entries


def parse_map_number(value):
    value = value.strip()
    if not value:
        raise ValueError("empty number")
    if value.startswith(("0x", "0X")):
        return int(value, 16)
    if all(ch in "0123456789abcdefABCDEF" for ch in value):
        return int(value, 16)
    return int(value, 0)


def parse_map_data(data):
    data = data.lstrip()
    if data.startswith("FBMAP"):
        return parse_map_text(data)
    if data.startswith("<"):
        return parse_map_xml(data)
    raise RuntimeError("unrecognized memory map format")


def add_segment(start, length, name, mem_type, perm):
    if length <= 0:
        return False
    end = start + length
    if idaapi.getseg(start) or idaapi.getseg(end - 1):
        log("skip %s (0x%08X-0x%08X), overlaps existing segment" % (name, start, end))
        return False
    seg = idaapi.segment_t()
    seg.start_ea = start
    seg.end_ea = end
    seg.bitness = 1  # 32-bit
    seg.perm = 0
    if not perm:
        if mem_type == "rom":
            perm = "r-x"
        elif mem_type == "io":
            perm = "rw-"
        else:
            perm = "rwx"
    if "r" in perm:
        seg.perm |= idaapi.SEGPERM_READ
    if "w" in perm:
        seg.perm |= idaapi.SEGPERM_WRITE
    if "x" in perm:
        seg.perm |= idaapi.SEGPERM_EXEC
    if mem_type == "io":
        segclass = "IO"
    elif "x" in perm:
        segclass = "CODE"
    else:
        segclass = "DATA"
    name = unique_seg_name(name)
    flags = idaapi.ADDSEG_NOSREG | idaapi.ADDSEG_QUIET
    if not idaapi.add_segm_ex(seg, name, segclass, flags):
        log("failed to add %s (0x%08X-0x%08X)" % (name, start, end))
        return False
    return True


def collect_overlapping_segments(entries):
    overlaps = {}
    for start, length, _name, _mem_type, _perm in entries:
        if length <= 0:
            continue
        end = start + length
        seg = idaapi.getseg(start)
        if seg is None:
            seg = ida_segment.get_next_seg(start)
        while seg and seg.start_ea < end:
            if seg.end_ea > start:
                overlaps[seg.start_ea] = seg
            seg = ida_segment.get_next_seg(seg.end_ea)
    return list(overlaps.values())


def remove_overlapping_segments(entries):
    overlaps = collect_overlapping_segments(entries)
    if not overlaps:
        return True
    choice = ida_kernwin.ask_yn(
        ida_kernwin.ASKBTN_BTN1,
        "Existing segments overlap the Firebird memory map.\n"
        "Remove overlapping segments and recreate them?"
    )
    if choice != ida_kernwin.ASKBTN_BTN1:
        log("segment import canceled")
        return False
    for seg in overlaps:
        ida_segment.del_segm(seg.start_ea, ida_segment.SEGMOD_KEEP | ida_segment.SEGMOD_SILENT)
    return True


def set_arm_processor():
    try:
        idaapi.set_processor_type("arm:ARMv5TEJ", idaapi.SETPROC_LOADER)
    except Exception:
        pass


def prompt_gdb_connection():
    global _last_gdb_host, _last_gdb_port
    host = ida_kernwin.ask_str(_last_gdb_host, 0, "GDB host")
    if host is None or not host.strip():
        return None
    port = ida_kernwin.ask_long(_last_gdb_port, "GDB port")
    if port is None or port <= 0:
        return None
    _last_gdb_host = host.strip()
    _last_gdb_port = int(port)
    return _last_gdb_host, _last_gdb_port


def prompt_app_path():
    global _last_app_path
    app_path = ida_nalt.get_input_file_path()
    if app_path and os.path.exists(app_path):
        _last_app_path = app_path
        return app_path
    if _last_app_path and os.path.exists(_last_app_path):
        return _last_app_path
    app_path = ida_kernwin.ask_file(False, "*.*", "Select application binary")
    if not app_path:
        choice = ida_kernwin.ask_yn(ida_kernwin.ASKBTN_BTN1,
                                    "No binary selected. Start debugger without a binary?")
        if choice == ida_kernwin.ASKBTN_BTN1:
            _last_app_path = ""
            return ""
        return None
    _last_app_path = app_path
    return app_path


def configure_gdb_debugger(host, port):
    if not ida_dbg.load_debugger("gdb", True):
        log("failed to load GDB debugger")
        return False
    ida_dbg.set_remote_debugger(host, "", port)
    return True


def start_debugger_session():
    if ida_dbg.is_debugger_on():
        log("debugger already active")
        return False
    conn = prompt_gdb_connection()
    if not conn:
        return False
    app_path = prompt_app_path()
    if app_path is None:
        return False
    if app_path and not os.path.exists(app_path):
        log("application path not found")
        return False
    host, port = conn
    if not configure_gdb_debugger(host, port):
        return False
    result = ida_dbg.start_process(app_path, "", "")
    if result != 1:
        log("failed to start debugger (result=%d)" % result)
        return False
    return True


def load_memmap_from_data(data):
    entries = parse_map_data(data)
    if not entries:
        log("no memory map entries found")
        return False
    if not remove_overlapping_segments(entries):
        return False
    set_arm_processor()
    added = 0
    for start, length, name, mem_type, perm in entries:
        if add_segment(start, length, name, mem_type, perm):
            added += 1
    log("loaded %d segments" % added)
    return True


def load_memmap_from_file():
    path = ida_kernwin.ask_file(False, "*.*", "Select Firebird memory map")
    if not path:
        return False
    if not os.path.exists(path):
        log("memory map not found: %s" % path)
        return False
    with open(path, "r", encoding="utf-8") as handle:
        data = handle.read()
    return load_memmap_from_data(data)


def send_monitor_command(cmd):
    ok, resp = ida_dbg.send_dbg_command(cmd)
    if not ok:
        log("debugger command failed: %s" % resp)
        return None
    return resp


def load_memmap_from_debugger():
    if not ida_dbg.is_debugger_on():
        log("debugger is not active; start debugging or attach first")
        return False
    resp = send_monitor_command("monitor fb memmap text")
    if resp and load_memmap_from_data(resp):
        return True
    resp = send_monitor_command("monitor fb memmap")
    if resp and load_memmap_from_data(resp):
        return True
    log("no usable memory map in response; use file import instead")
    return False


def show_target_info():
    if not ida_dbg.is_debugger_on():
        log("debugger is not active; start debugging or attach first")
        return False
    resp = send_monitor_command("monitor fb info")
    if resp is None:
        return False
    info = resp.strip()
    if not info:
        log("no info returned")
        return False
    ida_kernwin.info("Firebird target info:\n%s" % info)
    return True


def load_type_info_placeholder():
    ida_kernwin.info(
        "Firebird type info support is not implemented yet.\n"
        "This entry is reserved for future use."
    )
    return True


class FirebirdActionHandler(ida_kernwin.action_handler_t):
    def __init__(self, callback):
        super(FirebirdActionHandler, self).__init__()
        self._callback = callback

    def activate(self, ctx):
        return 1 if self._callback() else 0

    def update(self, ctx):
        return ida_kernwin.AST_ENABLE_ALWAYS


def register_action(action_id, text, callback, menu_path):
    desc = ida_kernwin.action_desc_t(
        action_id,
        text,
        FirebirdActionHandler(callback),
        None,
        None,
        -1
    )
    if not ida_kernwin.register_action(desc):
        return False
    ida_kernwin.attach_action_to_menu(menu_path, action_id, ida_kernwin.SETMENU_APP)
    return True


def unregister_action(action_id, menu_path):
    try:
        ida_kernwin.detach_action_from_menu(menu_path, action_id)
    except Exception:
        pass
    ida_kernwin.unregister_action(action_id)


class FirebirdPlugin(idaapi.plugin_t):
    flags = idaapi.PLUGIN_UNL
    comment = PLUGIN_HELP
    help = PLUGIN_HELP
    wanted_name = PLUGIN_NAME
    wanted_hotkey = ""

    def init(self):
        ensure_menu()
        register_action(
            "firebird:start_debugger",
            "Firebird: Start debugger (GDB remote)",
            start_debugger_session,
            _menu_path
        )
        register_action(
            "firebird:load_memmap_dbg",
            "Firebird: Load memory map (debugger)",
            load_memmap_from_debugger,
            _menu_path
        )
        register_action(
            "firebird:load_memmap_file",
            "Firebird: Load memory map (file)",
            load_memmap_from_file,
            _menu_path
        )
        register_action(
            "firebird:show_info",
            "Firebird: Show target info",
            show_target_info,
            _menu_path
        )
        register_action(
            "firebird:types_placeholder",
            "Firebird: Apply type info (future)",
            load_type_info_placeholder,
            _menu_path
        )
        return idaapi.PLUGIN_KEEP

    def run(self, arg):
        load_memmap_from_debugger()

    def term(self):
        unregister_action("firebird:start_debugger", _menu_path)
        unregister_action("firebird:load_memmap_dbg", _menu_path)
        unregister_action("firebird:load_memmap_file", _menu_path)
        unregister_action("firebird:show_info", _menu_path)
        unregister_action("firebird:types_placeholder", _menu_path)
        if _menu_created:
            ida_kernwin.delete_menu(FIREBIRD_MENU_NAME)


def PLUGIN_ENTRY():
    return FirebirdPlugin()
