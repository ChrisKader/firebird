# Firebird IDA pre-debug setup script.
# Usage:
#   ida64 -S"/path/to/firebird_load.py /path/to/memmap.xml" <input>
#   or in IDA: File -> Script file... and select this script.

import os
import xml.etree.ElementTree as ET

import idaapi
import ida_kernwin
import idc


def log(message):
    ida_kernwin.msg("[firebird] %s\n" % message)


def get_map_path():
    args = getattr(idc, "ARGV", [])
    if len(args) >= 2:
        return args[1]
    return ida_kernwin.ask_file(False, "*.xml", "Select Firebird memory map XML")


def parse_map(path):
    tree = ET.parse(path)
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
        entries.append((int(start, 0), int(length, 0), name, mem_type))
    return entries


def add_segment(start, length, name, mem_type):
    if length <= 0:
        return
    end = start + length
    if idaapi.getseg(start) or idaapi.getseg(end - 1):
        log("skip %s (0x%08X-0x%08X), overlaps existing segment" % (name, start, end))
        return
    seg = idaapi.segment_t()
    seg.start_ea = start
    seg.end_ea = end
    seg.bitness = 1  # 32-bit
    seg.perm = idaapi.SEGPERM_READ
    if mem_type == "rom":
        seg.perm |= idaapi.SEGPERM_EXEC
        segclass = "CODE"
    else:
        seg.perm |= idaapi.SEGPERM_WRITE
        segclass = "DATA"
    flags = idaapi.ADDSEG_NOSREG | idaapi.ADDSEG_QUIET
    if not idaapi.add_segm_ex(seg, name, segclass, flags):
        log("failed to add %s (0x%08X-0x%08X)" % (name, start, end))


def main():
    map_path = get_map_path()
    if not map_path:
        log("no memory map selected")
        return
    if not os.path.exists(map_path):
        log("memory map not found: %s" % map_path)
        return
    try:
        idaapi.set_processor_type("arm", idaapi.SETPROC_LOADER)
    except Exception:
        pass
    entries = parse_map(map_path)
    for start, length, name, mem_type in entries:
        add_segment(start, length, name, mem_type)
    log("loaded %d segments from %s" % (len(entries), map_path))


if __name__ == "__main__":
    main()
