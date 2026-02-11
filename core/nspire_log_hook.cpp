#include "nspire_log_hook.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cpu.h"
#include "debug.h"
#include "emu.h"
#include "mem.h"
#include "translate.h"

namespace {

struct Anchor {
    const char *text;
    bool core;
};

static constexpr std::array<Anchor, 12> kAnchors = {{
    { "TI_LOG_ZipFileWrite",         true  },
    { "addFilesToZipFile",           true  },
    { "addLargeFilesToZipFile",      true  },
    { "logToErrorFile",              true  },
    { "L:/%s_debug_log_%d.txt",      true  },
    { "L:/%s_boot_log_%d.txt",       true  },
    { "L:/%s_install_log_%d.txt",    false },
    { "L:/%s_error_log_%d.txt",      false },
    { "L:/%s_reboot_%d.txt",         false },
    { "/logs/%s_stats_log.txt",      false },
    { "/logs/%s_metrics_log.txt",    false },
    { "/logs/debug_temp_log_1.txt",  false },
}};

/* Seed dispatch entry points (used as fast-path hints).
 * Dynamic signature discovery is used when these offsets differ by version. */
static constexpr std::array<uint32_t, 2> kDispatchSeedHooks = {{
    0x100AEADCu,
    0x132415CCu,
}};

struct FilterPatch {
    uint32_t addr;
    uint32_t original;
    uint32_t patch;
};

/* Option B: bypass runtime filter checks in TI-Nspire.bin debug dispatcher. */
static constexpr std::array<FilterPatch, 3> kFilterBypassFixed = {{
    { 0x100AEB38u, 0x0A000009u, 0xE1A00000u }, /* BEQ -> NOP (master enable) */
    { 0x100AEB50u, 0x0A000003u, 0xE1A00000u }, /* BEQ -> NOP (component mask) */
    { 0x100AEB60u, 0x1A000005u, 0xEA000005u }, /* BNE -> B   (level mask) */
}};

static constexpr uint32_t kFastPollInterval = 4096u;
static constexpr uint32_t kSlowScanPollInterval = 200000u;
static constexpr uint32_t kDispatchFullScanPollInterval = 250000u;

struct Candidate {
    uint32_t entry = 0;
    uint32_t refs = 0;
    uint32_t mask = 0;
};

static bool config_checked = false;
static bool hook_enabled = true;
static bool hooks_installed = false;
static bool auto_scan_fallback = false;
static bool filter_bypass_enabled = false;
static bool filter_bypass_installed = false;
static bool scan_attempted = false;
static uint64_t poll_counter = 0;
static size_t last_anchor_count = 0;
static size_t last_candidate_count = 0;
static uint64_t total_hook_hits = 0;
static uint64_t total_lines_emitted = 0;
static std::unordered_set<uint32_t> hook_addrs;
static std::unordered_set<uint32_t> dispatch_hook_addrs;
static std::unordered_map<uint32_t, uint32_t> hook_anchor_mask;
static std::unordered_map<uint32_t, uint64_t> hook_hits_by_pc;
static std::unordered_map<uint32_t, std::string> last_file_for_pc;
static std::string current_file;
static std::string last_emitted_line;
static std::array<FilterPatch, 3> filter_bypass_runtime = {{
    { 0u, 0u, 0u }, { 0u, 0u, 0u }, { 0u, 0u, 0u }
}};
static bool filter_bypass_runtime_valid = false;
static bool dispatch_scan_attempted = false;
static uint64_t dispatch_last_full_scan_poll = 0;
static uint32_t dispatch_write_probe_counter = 0;

static bool equals_nocase(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    while (*a && *b) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return *a == 0 && *b == 0;
}

static void check_config_once()
{
    if (config_checked)
        return;
    config_checked = true;
    const char *env = std::getenv("FIREBIRD_NSPIRE_LOG_HOOK");
    if (!env || !*env) {
        hook_enabled = true;
    } else if (equals_nocase(env, "0") || equals_nocase(env, "false") || equals_nocase(env, "off")) {
        hook_enabled = false;
    } else {
        hook_enabled = true;
    }

    const char *env_scan = std::getenv("FIREBIRD_NSPIRE_LOG_AUTOSCAN");
    if (!env_scan || !*env_scan) {
        auto_scan_fallback = false; /* disabled by default; write-time discovery handles most cases */
    } else if (equals_nocase(env_scan, "1") || equals_nocase(env_scan, "true") || equals_nocase(env_scan, "on")) {
        auto_scan_fallback = true;
    } else {
        auto_scan_fallback = false;
    }

    const char *env_bypass = std::getenv("FIREBIRD_NSPIRE_LOG_BYPASS");
    if (!env_bypass || !*env_bypass) {
        filter_bypass_enabled = false;
    } else if (equals_nocase(env_bypass, "1") || equals_nocase(env_bypass, "true") || equals_nocase(env_bypass, "on")) {
        filter_bypass_enabled = true;
    } else {
        filter_bypass_enabled = false;
    }
}

static bool read_word_mapped(uint32_t addr, uint32_t *out)
{
    if (!mem_areas[1].ptr || mem_areas[1].size < 4)
        return false;
    const uint32_t base = mem_areas[1].base;
    const uint32_t end = base + mem_areas[1].size;
    if (addr < base || addr + 4u > end)
        return false;
    std::memcpy(out, mem_areas[1].ptr + (addr - base), sizeof(uint32_t));
    return true;
}

static bool read_u32_va(uint32_t addr, uint32_t *out)
{
    uint32_t *p = static_cast<uint32_t *>(virt_mem_ptr(addr, 4));
    if (!p)
        return false;
    std::memcpy(out, p, sizeof(uint32_t));
    return true;
}

static bool write_u32_va(uint32_t addr, uint32_t value)
{
    uint32_t *p = static_cast<uint32_t *>(virt_mem_ptr(addr, 4));
    if (!p)
        return false;
    std::memcpy(p, &value, sizeof(uint32_t));
    return true;
}

static bool read_u32_code(uint32_t addr, uint32_t *out)
{
    if (read_u32_va(addr, out))
        return true;
    return read_word_mapped(addr, out);
}

static bool write_u32_code(uint32_t addr, uint32_t value)
{
    if (write_u32_va(addr, value))
        return true;

    if (!mem_areas[1].ptr || mem_areas[1].size < 4)
        return false;
    const uint32_t base = mem_areas[1].base;
    const uint32_t end = base + mem_areas[1].size;
    if (addr < base || addr + 4u > end)
        return false;
    std::memcpy(mem_areas[1].ptr + (addr - base), &value, sizeof(uint32_t));
    return true;
}

static bool is_known_dispatch_hook(uint32_t pc)
{
    pc &= ~3u;
    return dispatch_hook_addrs.find(pc) != dispatch_hook_addrs.end();
}

static uint32_t select_primary_dispatch_entry(void)
{
    uint32_t best = 0;
    for (uint32_t addr : dispatch_hook_addrs) {
        if (addr < 0x10000000u || addr >= 0x13200000u)
            continue; /* Prefer OS image dispatcher for filter bypass. */
        if (!best || addr < best)
            best = addr;
    }
    if (best)
        return best;
    return 0;
}

static std::vector<uint32_t> find_string_addresses(const char *needle, size_t max_hits = 32)
{
    std::vector<uint32_t> hits;
    if (!needle || !*needle || !mem_areas[1].ptr || mem_areas[1].size == 0)
        return hits;

    const size_t nlen = std::strlen(needle);
    if (nlen == 0 || nlen > mem_areas[1].size)
        return hits;

    const uint8_t *mem = mem_areas[1].ptr;
    const uint32_t base = mem_areas[1].base;
    const size_t limit = mem_areas[1].size - nlen;
    for (size_t off = 0; off <= limit; ++off) {
        if (mem[off] != static_cast<uint8_t>(needle[0]))
            continue;
        if (std::memcmp(mem + off, needle, nlen) != 0)
            continue;
        hits.push_back(base + static_cast<uint32_t>(off));
        if (hits.size() >= max_hits)
            break;
    }
    return hits;
}

static bool set_exec_breakpoint(uint32_t addr, bool enabled)
{
    void *ptr = virt_mem_ptr(addr & ~3u, 4);
    if (!ptr)
        return false;
    uint32_t &flags = RAM_FLAGS(ptr);
    if (enabled) {
        if (flags & RF_CODE_TRANSLATED)
            flush_translations();
        flags |= RF_EXEC_BREAKPOINT;
    } else {
        flags &= ~RF_EXEC_BREAKPOINT;
    }
    return true;
}

static bool is_arm_push_prologue(uint32_t op)
{
    /* STMDB sp!, {...., lr} */
    if ((op & 0x0FFF0000u) == 0x092D0000u && (op & (1u << 14)))
        return true;
    /* STR lr, [sp, #-4]! */
    if ((op & 0x0FFFFFFFu) == 0x052DE004u)
        return true;
    return false;
}

static uint32_t find_function_prologue(uint32_t site)
{
    const uint32_t max_back = 0x200;
    for (uint32_t back = 0; back <= max_back; back += 4) {
        uint32_t pc = site - back;
        uint32_t op = 0;
        if (!read_word_mapped(pc, &op))
            break;
        if (is_arm_push_prologue(op))
            return pc;
    }
    return site;
}

static bool is_arm_ldr_literal(uint32_t op)
{
    /* ARM mode: LDR Rd, [PC, #imm12] (pre-index immediate) */
    if (((op >> 26) & 0x3) != 0x1)  /* data transfer class */
        return false;
    if (((op >> 25) & 0x1) != 0x0)  /* immediate form */
        return false;
    if (((op >> 24) & 0x1) != 0x1)  /* pre-indexed */
        return false;
    if (((op >> 20) & 0x1) != 0x1)  /* LDR (not STR) */
        return false;
    if (((op >> 16) & 0xF) != 0xF)  /* base register PC */
        return false;
    return true;
}

static bool is_arm_movw(uint32_t op)
{
    return (op & 0x0FF00000u) == 0x03000000u;
}

static bool is_arm_movt(uint32_t op)
{
    return (op & 0x0FF00000u) == 0x03400000u;
}

static uint32_t arm_mov_imm16(uint32_t op)
{
    return ((op >> 4) & 0xF000u) | (op & 0x0FFFu);
}

static uint32_t arm_rd(uint32_t op)
{
    return (op >> 12) & 0xFu;
}

static bool looks_like_dispatch_signature(uint32_t entry)
{
    uint32_t first = 0;
    if (!read_word_mapped(entry, &first) || !is_arm_push_prologue(first))
        return false;

    bool has_master_check = false;
    bool has_component_check = false;
    bool has_level_check = false;
    for (uint32_t off = 8; off <= 0x140; off += 4) {
        uint32_t prev = 0, cur = 0;
        if (!read_word_mapped(entry + off - 4, &prev) || !read_word_mapped(entry + off, &cur))
            break;
        if (!has_master_check && prev == 0xE3500000u && (cur & 0xFF000000u) == 0x0A000000u)
            has_master_check = true;
        if (!has_component_check && prev == 0xE11C0005u && (cur & 0xFF000000u) == 0x0A000000u)
            has_component_check = true;
        if (!has_level_check && prev == 0xE1190003u && (cur & 0xFF000000u) == 0x1A000000u)
            has_level_check = true;
        if (has_master_check && has_component_check && has_level_check)
            return true;
    }
    return false;
}

static void discover_dispatch_hooks(bool allow_full_scan)
{
    dispatch_scan_attempted = true;
    std::unordered_set<uint32_t> found;

    for (uint32_t addr : kDispatchSeedHooks) {
        if (looks_like_dispatch_signature(addr))
            found.insert(addr);
    }

    /* Fast path: seed addresses still valid for this firmware build. */
    if (!found.empty()) {
        dispatch_hook_addrs = std::move(found);
        return;
    }

    if (allow_full_scan && emulate_cx2 && mem_areas[1].ptr) {
        const uint32_t begin = std::max(mem_areas[1].base, 0x10000000u);
        const uint32_t end_limit = mem_areas[1].base + mem_areas[1].size;
        for (uint32_t pc = begin; pc + 4u <= end_limit; pc += 4u) {
            uint32_t op = 0;
            if (!read_word_mapped(pc, &op))
                continue;
            if (!is_arm_push_prologue(op))
                continue;
            if (!looks_like_dispatch_signature(pc))
                continue;
            found.insert(pc);
            if (found.size() >= 4)
                break;
        }
        dispatch_last_full_scan_poll = poll_counter;
    }

    dispatch_hook_addrs = std::move(found);
}

static bool try_register_dispatch_entry(uint32_t entry, bool install_now)
{
    entry &= ~3u;
    if (dispatch_hook_addrs.find(entry) != dispatch_hook_addrs.end())
        return true;
    if (!looks_like_dispatch_signature(entry))
        return false;

    dispatch_hook_addrs.insert(entry);
    if (!install_now || !hook_enabled)
        return true;

    if (set_exec_breakpoint(entry, true)) {
        hook_addrs.insert(entry);
        hooks_installed = true;
    }
    return true;
}

static bool read_cstr_va(uint32_t addr, std::string &out, size_t max_len = 256)
{
    out.clear();
    if (!addr)
        return false;

    bool ended = false;
    for (size_t i = 0; i < max_len; i++) {
        uint8_t *p = static_cast<uint8_t *>(virt_mem_ptr(addr + static_cast<uint32_t>(i), 1));
        if (!p)
            return false;
        uint8_t c = *p;
        if (!c) {
            ended = true;
            break;
        }
        if ((c < 0x20 || c > 0x7e) && c != '\n' && c != '\r' && c != '\t')
            return false;
        out.push_back(static_cast<char>(c));
    }
    if (!ended || out.size() < 2)
        return false;
    return true;
}

static std::string basename_from_path(const std::string &path)
{
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

static std::string sanitize_file_label(std::string s)
{
    if (s.empty())
        return "NspireLogs";
    s = basename_from_path(s);

    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F || c == '[' || c == ']')
            continue;
        if (std::isspace(c))
            break;
        out.push_back(static_cast<char>(c));
        if (out.size() >= 64)
            break;
    }
    if (out.empty())
        return "NspireLogs";
    return out;
}

static int score_format_candidate(const std::string &s)
{
    if (s.size() < 2)
        return -1000;

    int score = 0;
    bool has_alpha = false;
    for (char ch : s) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha)
        return -1000;

    if (s.find('%') != std::string::npos)
        score += 40;
    if (s.find(' ') != std::string::npos)
        score += 12;
    if (s.find(':') != std::string::npos)
        score += 6;
    if (s.find('\t') != std::string::npos)
        score += 3;

    if (s.find("../") != std::string::npos || s.find("/src/") != std::string::npos)
        score -= 25; /* likely source file path, not a format string */
    if (s.size() >= 2 && s.rfind(".c") == s.size() - 2)
        score -= 20;
    if (s.rfind("L:/", 0) == 0 || s.rfind("/", 0) == 0)
        score -= 10;

    return score;
}

static bool looks_like_log_path(const std::string &s)
{
    return s.find("L:/") != std::string::npos
        || s.find("/logs/") != std::string::npos
        || s.find("/documents/") != std::string::npos
        || s.find("/metric/") != std::string::npos
        || s.find(".txt") != std::string::npos
        || s.find(".zip") != std::string::npos;
}

static bool looks_like_noise(const std::string &s)
{
    if (s.empty())
        return true;
    if (s.find("../device/ti_debug/logging/src/") != std::string::npos)
        return true;
    if (s.find("%s") != std::string::npos && s.find("log") == std::string::npos)
        return true;
    bool has_alnum = false;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            has_alnum = true;
            break;
        }
    }
    return !has_alnum;
}

static bool should_suppress_nlog_line(const std::string &label, const std::string &line)
{
    /* Reduce known high-frequency noise from allocator churn. */
    if (label == "ti_os_memory.c" && line.find("WLAN FREE") != std::string::npos)
        return true;
    return false;
}

static std::string strip_mask_field(std::string line)
{
    size_t pos = line.find("mask=");
    if (pos == std::string::npos)
        return line;

    size_t hex_begin = pos + 5;
    size_t hex_end = hex_begin;
    while (hex_end < line.size() && std::isxdigit(static_cast<unsigned char>(line[hex_end])))
        hex_end++;
    if (hex_end == hex_begin)
        return line;

    size_t erase_begin = pos;
    if (erase_begin > 0 && line[erase_begin - 1] == ' ')
        erase_begin--;
    while (hex_end < line.size() && std::isspace(static_cast<unsigned char>(line[hex_end])))
        hex_end++;

    line.erase(erase_begin, hex_end - erase_begin);
    return line;
}

static void emit_tagged_line(const std::string &file, const std::string &line)
{
    const std::string cleaned = strip_mask_field(line);
    if (cleaned.empty())
        return;
    const std::string label = sanitize_file_label(file);
    if (should_suppress_nlog_line(label, cleaned))
        return;
    std::string clipped = cleaned;
    if (clipped.size() > 1024) {
        clipped.resize(1024);
        clipped += "...";
    }
    std::string formatted = "[" + label + "] " + clipped;
    if (formatted == last_emitted_line)
        return;
    last_emitted_line = formatted;
    total_lines_emitted++;
    gui_nlog_printf("[%s] %s\n", label.c_str(), clipped.c_str());
}

static void emit_multiline(const std::string &file, const std::string &text)
{
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        std::string line = (end == std::string::npos) ? text.substr(start)
                                                       : text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        emit_tagged_line(file, line);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
}

static std::string fallback_file_from_anchor_mask(uint32_t mask)
{
    for (size_t i = 0; i < kAnchors.size(); i++) {
        if ((mask & (1u << i)) == 0)
            continue;
        std::string s = kAnchors[i].text;
        if (!looks_like_log_path(s))
            continue;
        if (s.rfind("L:/", 0) == 0)
            s = s.substr(3);
        std::string b = basename_from_path(s);
        if (!b.empty())
            return b;
    }
    return "NspireLogs";
}

static bool format_dispatch_printf(const std::string &fmt, uint32_t args_base, std::string &out)
{
    out.clear();
    bool consumed_args = false;
    uint32_t arg_index = 0;

    for (size_t i = 0; i < fmt.size(); ) {
        const char c = fmt[i++];
        if (c != '%') {
            out.push_back(c);
            continue;
        }
        if (i < fmt.size() && fmt[i] == '%') {
            out.push_back('%');
            i++;
            continue;
        }

        /* Skip flags/width/precision/length and consume '*' arguments when present. */
        while (i < fmt.size() && std::strchr("-+ #0", fmt[i]))
            i++;
        if (i < fmt.size() && fmt[i] == '*') {
            uint32_t ignored = 0;
            if (!read_u32_va(args_base + arg_index * 4u, &ignored))
                return false;
            arg_index++;
            consumed_args = true;
            i++;
        } else {
            while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i])))
                i++;
        }
        if (i < fmt.size() && fmt[i] == '.') {
            i++;
            if (i < fmt.size() && fmt[i] == '*') {
                uint32_t ignored = 0;
                if (!read_u32_va(args_base + arg_index * 4u, &ignored))
                    return false;
                arg_index++;
                consumed_args = true;
                i++;
            } else {
                while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i])))
                    i++;
            }
        }
        if (i < fmt.size() && std::strchr("hljztL", fmt[i])) {
            const char len = fmt[i++];
            if ((len == 'h' || len == 'l') && i < fmt.size() && fmt[i] == len)
                i++;
        }
        if (i >= fmt.size())
            break;

        const char spec = fmt[i++];
        if (spec == 'n') {
            out += "<%n>";
            continue;
        }

        uint32_t raw = 0;
        if (!read_u32_va(args_base + arg_index * 4u, &raw)) {
            out += "<arg?>";
            continue;
        }
        arg_index++;
        consumed_args = true;

        char tmp[64];
        switch (spec) {
        case 's': {
            std::string s;
            if (raw && read_cstr_va(raw, s, 512))
                out += s;
            else {
                std::snprintf(tmp, sizeof(tmp), "<str@%08x>", raw);
                out += tmp;
            }
            break;
        }
        case 'd':
        case 'i':
            std::snprintf(tmp, sizeof(tmp), "%d", static_cast<int32_t>(raw));
            out += tmp;
            break;
        case 'u':
            std::snprintf(tmp, sizeof(tmp), "%u", raw);
            out += tmp;
            break;
        case 'x':
            std::snprintf(tmp, sizeof(tmp), "%x", raw);
            out += tmp;
            break;
        case 'X':
            std::snprintf(tmp, sizeof(tmp), "%X", raw);
            out += tmp;
            break;
        case 'p':
            std::snprintf(tmp, sizeof(tmp), "0x%08x", raw);
            out += tmp;
            break;
        case 'c': {
            const unsigned char ch = static_cast<unsigned char>(raw & 0xFFu);
            if (std::isprint(ch))
                out.push_back(static_cast<char>(ch));
            else {
                std::snprintf(tmp, sizeof(tmp), "\\x%02x", ch);
                out += tmp;
            }
            break;
        }
        default:
            std::snprintf(tmp, sizeof(tmp), "<%%%c:%08x>", spec, raw);
            out += tmp;
            break;
        }
    }

    return consumed_args;
}

static bool capture_known_dispatch_log(uint32_t pc)
{
    if (!is_known_dispatch_hook(pc))
        return false;

    std::string source_file;
    (void)read_cstr_va(arm.reg[0], source_file, 256);
    std::string file = sanitize_file_label(source_file);
    if (file.empty())
        file = "NspireLogs";

    const uint32_t sp = arm.reg[13];
    const std::array<uint32_t, 4> candidate_slots = {{
        sp, sp + 4u, sp + 8u, sp + 0x70u
    }};

    int best_score = -1000;
    uint32_t best_slot = 0;
    std::string best_fmt;
    for (uint32_t slot : candidate_slots) {
        uint32_t fmt_ptr = 0;
        if (!read_u32_va(slot, &fmt_ptr))
            continue;
        std::string fmt;
        if (!read_cstr_va(fmt_ptr, fmt, 512))
            continue;
        int score = score_format_candidate(fmt);
        if (score > best_score) {
            best_score = score;
            best_slot = slot;
            best_fmt = std::move(fmt);
        }
    }
    if (best_score <= -1000) {
        emit_tagged_line(file, "<unparsed dispatch log>");
        return true;
    }

    std::string rendered;
    if (!format_dispatch_printf(best_fmt, best_slot + 4u, rendered))
        rendered = best_fmt;

    /* If parsing produced too many unresolved placeholders,
     * prefer raw format to avoid misleading garbage output. */
    size_t unresolved = 0;
    size_t pos = 0;
    while ((pos = rendered.find("<str@", pos)) != std::string::npos) {
        unresolved++;
        pos += 5;
    }
    if (unresolved >= 2)
        rendered = best_fmt;

    const uint32_t line = arm.reg[1];
    const uint32_t level = arm.reg[3];
    const bool sane_line = line > 0 && line < 100000;
    const bool sane_level = level <= 7;

    std::string display_label = file;
    if (sane_line && sane_level) {
        display_label = std::to_string(level) + ":" + file + ":" + std::to_string(line);
        emit_multiline(display_label, rendered);
        return true;
    }

    emit_multiline(display_label, rendered);
    return true;
}

static bool capture_register_strings(uint32_t pc)
{
    if (capture_known_dispatch_log(pc))
        return true;

    std::array<uint32_t, 4> regs = { arm.reg[0], arm.reg[1], arm.reg[2], arm.reg[3] };
    std::vector<std::string> strings;
    std::string file = current_file;

    for (uint32_t r : regs) {
        std::string s;
        if (!read_cstr_va(r, s))
            continue;
        strings.push_back(s);
        if (looks_like_log_path(s)) {
            std::string b = basename_from_path(s);
            if (!b.empty()) {
                file = b;
                current_file = b;
                last_file_for_pc[pc] = b;
            }
        }
    }

    /* Also inspect likely stack-passed arguments (ARM ABI after r0-r3). */
    const uint32_t sp = arm.reg[13];
    for (uint32_t off = 0; off < 64; off += 4) {
        uint32_t v = 0;
        uint32_t *p = static_cast<uint32_t *>(virt_mem_ptr(sp + off, 4));
        if (!p)
            break;
        std::memcpy(&v, p, sizeof(v));
        std::string s;
        if (!read_cstr_va(v, s))
            continue;
        strings.push_back(s);
        if (looks_like_log_path(s)) {
            std::string b = basename_from_path(s);
            if (!b.empty()) {
                file = b;
                current_file = b;
                last_file_for_pc[pc] = b;
            }
        }
    }

    if (file.empty()) {
        auto it = last_file_for_pc.find(pc);
        if (it != last_file_for_pc.end())
            file = it->second;
    }
    if (file.empty())
        file = "NspireLogs";

    bool emitted = false;
    for (const std::string &s : strings) {
        if (looks_like_log_path(s)) {
            emit_tagged_line(file, s);
            emitted = true;
            continue;
        }
        if (looks_like_noise(s))
            continue;
        emit_multiline(file, s);
        emitted = true;
    }

    if (emitted)
        return true;

    const uint32_t p = arm.reg[0];
    const uint32_t n = arm.reg[1];
    if (n > 0 && n <= 1024) {
        std::string buf;
        buf.reserve(n);
        bool printable = true;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *q = static_cast<uint8_t *>(virt_mem_ptr(p + i, 1));
            if (!q) {
                printable = false;
                break;
            }
            uint8_t c = *q;
            if ((c < 0x20 || c > 0x7e) && c != '\n' && c != '\r' && c != '\t') {
                printable = false;
                break;
            }
            buf.push_back(static_cast<char>(c));
        }
        if (printable && !buf.empty()) {
            emit_multiline(file, buf);
            return true;
        }
    }

    uint64_t hit = ++hook_hits_by_pc[pc];
    const uint32_t mask = hook_anchor_mask.count(pc) ? hook_anchor_mask[pc] : 0;
    if (file == "NspireLogs")
        file = fallback_file_from_anchor_mask(mask);
    if (hit <= 3) {
        char tmp[160];
        std::snprintf(tmp, sizeof(tmp),
                      "<hook hit pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x>",
                      pc, arm.reg[0], arm.reg[1], arm.reg[2], arm.reg[3], arm.reg[13]);
        emit_tagged_line(file, tmp);
    }
    return false;
}

static bool discover_filter_bypass_patches(std::array<FilterPatch, 3> &out, bool *used_pattern_scan)
{
    if (used_pattern_scan)
        *used_pattern_scan = false;

    bool fixed_ok = true;
    for (size_t i = 0; i < kFilterBypassFixed.size(); i++) {
        uint32_t cur = 0;
        const FilterPatch &p = kFilterBypassFixed[i];
        if (!read_u32_code(p.addr, &cur) || (cur != p.original && cur != p.patch)) {
            fixed_ok = false;
            break;
        }
        out[i] = p;
    }
    if (fixed_ok)
        return true;

    if (!dispatch_scan_attempted || dispatch_hook_addrs.empty())
        discover_dispatch_hooks(true);
    const uint32_t entry = select_primary_dispatch_entry();
    if (!entry)
        return false;
    const uint32_t end = entry + 0x200u;
    uint32_t b1_addr = 0, b1_orig = 0;
    uint32_t b2_addr = 0, b2_orig = 0;
    uint32_t b3_addr = 0, b3_orig = 0;
    for (uint32_t pc = entry + 4u; pc < end; pc += 4u) {
        uint32_t prev = 0, cur = 0;
        if (!read_u32_code(pc - 4u, &prev) || !read_u32_code(pc, &cur))
            continue;

        if (!b1_addr && prev == 0xE3500000u && (cur & 0xFF000000u) == 0x0A000000u) {
            b1_addr = pc;
            b1_orig = cur;
            continue;
        }
        if (!b2_addr && prev == 0xE11C0005u && (cur & 0xFF000000u) == 0x0A000000u) {
            b2_addr = pc;
            b2_orig = cur;
            continue;
        }
        if (!b3_addr && prev == 0xE1190003u && (cur & 0xFF000000u) == 0x1A000000u) {
            b3_addr = pc;
            b3_orig = cur;
            continue;
        }
    }

    if (!b1_addr || !b2_addr || !b3_addr)
        return false;

    out[0] = { b1_addr, b1_orig, 0xE1A00000u };
    out[1] = { b2_addr, b2_orig, 0xE1A00000u };
    out[2] = { b3_addr, b3_orig, (b3_orig & 0x00FFFFFFu) | 0xEA000000u };
    if (used_pattern_scan)
        *used_pattern_scan = true;
    return true;
}

static bool apply_filter_bypass_patch(bool verbose)
{
    if (!filter_bypass_enabled || filter_bypass_installed || !emulate_cx2)
        return filter_bypass_installed;

    bool used_pattern_scan = false;
    std::array<FilterPatch, 3> plan = {{
        { 0u, 0u, 0u }, { 0u, 0u, 0u }, { 0u, 0u, 0u }
    }};
    if (!discover_filter_bypass_patches(plan, &used_pattern_scan)) {
        if (verbose)
            gui_nlog_printf("nlog: bypass could not locate filter check pattern.\n");
        return false;
    }

    std::array<uint32_t, 3> current = {{ 0, 0, 0 }};
    for (size_t i = 0; i < plan.size(); i++) {
        const FilterPatch &p = plan[i];
        if (!read_u32_code(p.addr, &current[i])) {
            if (verbose)
                gui_nlog_printf("nlog: bypass pending (addr %08x unmapped).\n", p.addr);
            return false;
        }
        if (current[i] != p.original && current[i] != p.patch) {
            if (verbose) {
                gui_nlog_printf("nlog: bypass signature mismatch at %08x (expected %08x, saw %08x).\n",
                                p.addr, p.original, current[i]);
            }
            return false;
        }
    }

    bool changed = false;
    for (size_t i = 0; i < plan.size(); i++) {
        const FilterPatch &p = plan[i];
        if (current[i] == p.patch)
            continue;
        if (!write_u32_code(p.addr, p.patch)) {
            if (verbose)
                gui_nlog_printf("nlog: bypass write failed at %08x.\n", p.addr);
            return false;
        }
        changed = true;
    }

    if (changed)
        flush_translations();
    filter_bypass_installed = true;
    filter_bypass_runtime = plan;
    filter_bypass_runtime_valid = true;
    if (verbose)
        gui_nlog_printf("nlog: bypass installed (3 filter checks patched%s).\n",
                        used_pattern_scan ? ", pattern-scan" : "");
    return true;
}

static void remove_filter_bypass_patch(bool verbose)
{
    if (!filter_bypass_installed)
        return;

    bool changed = false;
    const std::array<FilterPatch, 3> &plan = filter_bypass_runtime_valid ? filter_bypass_runtime
                                                                          : kFilterBypassFixed;
    for (const FilterPatch &p : plan) {
        if (!p.addr)
            continue;
        uint32_t cur = 0;
        if (!read_u32_code(p.addr, &cur))
            continue;
        if (cur == p.original)
            continue;
        if (cur != p.patch && verbose) {
            gui_nlog_printf("nlog: bypass restore warning at %08x (unexpected %08x).\n",
                            p.addr, cur);
        }
        if (write_u32_code(p.addr, p.original))
            changed = true;
    }
    if (changed)
        flush_translations();
    filter_bypass_installed = false;
    filter_bypass_runtime_valid = false;
    if (verbose)
        gui_nlog_printf("nlog: bypass removed.\n");
}

static int install_known_dispatch_hooks(bool verbose)
{
    const bool due_full_scan = (poll_counter - dispatch_last_full_scan_poll) >= kDispatchFullScanPollInterval;
    const bool force_full_scan = verbose || (dispatch_hook_addrs.empty() && due_full_scan);
    if (!dispatch_scan_attempted || dispatch_hook_addrs.empty() || force_full_scan) {
        discover_dispatch_hooks(force_full_scan);
    }

    int newly_installed = 0;
    int ready_candidates = 0;
    std::vector<uint32_t> candidates(dispatch_hook_addrs.begin(), dispatch_hook_addrs.end());

    for (uint32_t addr : candidates) {
        if (!looks_like_dispatch_signature(addr))
            continue;
        ready_candidates++;
        if (hook_addrs.find(addr) != hook_addrs.end())
            continue;
        if (!set_exec_breakpoint(addr, true))
            continue;
        hook_addrs.insert(addr);
        newly_installed++;
    }
    hooks_installed = !hook_addrs.empty();
    if (verbose) {
        if (newly_installed > 0) {
            gui_nlog_printf("nlog: installed %d dispatch hook(s) (%d candidate(s)).\n",
                            newly_installed, ready_candidates);
        } else if (ready_candidates == 0) {
            gui_nlog_printf("nlog: no mapped dispatch hook candidates yet.\n");
        }
    }
    return newly_installed;
}

static int scan_and_install_hooks()
{
    scan_attempted = true;

    if (!emulate_cx2 || !mem_areas[1].ptr)
        return 0;

    std::unordered_map<uint32_t, uint32_t> anchor_mask_by_addr;
    uint32_t discovered_core_mask = 0;
    for (size_t i = 0; i < kAnchors.size(); i++) {
        std::vector<uint32_t> addrs = find_string_addresses(kAnchors[i].text);
        for (uint32_t addr : addrs)
            anchor_mask_by_addr[addr] |= (1u << i);
        if (!addrs.empty() && kAnchors[i].core)
            discovered_core_mask |= (1u << i);
    }
    last_anchor_count = anchor_mask_by_addr.size();
    if (anchor_mask_by_addr.empty() || discovered_core_mask == 0)
        return 0;

    std::unordered_map<uint32_t, Candidate> candidates;
    const uint32_t begin = std::max(mem_areas[1].base, 0x10000000u);
    const uint32_t end_limit = mem_areas[1].base + mem_areas[1].size;
    for (uint32_t pc = begin; pc + 4 <= end_limit; pc += 4) {
        uint32_t op = 0;
        if (!read_word_mapped(pc, &op))
            continue;

        if (is_arm_ldr_literal(op)) {
            const bool add = ((op >> 23) & 1) != 0;
            const uint32_t imm12 = op & 0xFFFu;
            uint32_t lit = pc + 8;
            lit = add ? (lit + imm12) : (lit - imm12);

            uint32_t value = 0;
            if (read_word_mapped(lit, &value)) {
                auto it = anchor_mask_by_addr.find(value);
                if (it != anchor_mask_by_addr.end()) {
                    uint32_t entry = find_function_prologue(pc);
                    Candidate &c = candidates[entry];
                    c.entry = entry;
                    c.refs++;
                    c.mask |= it->second;
                }
            }
        }

        if (is_arm_movw(op)) {
            const uint32_t rd = arm_rd(op);
            const uint32_t low = arm_mov_imm16(op);
            for (uint32_t d = 4; d <= 16; d += 4) {
                uint32_t op2 = 0;
                if (!read_word_mapped(pc + d, &op2))
                    break;
                if (!is_arm_movt(op2) || arm_rd(op2) != rd)
                    continue;
                const uint32_t high = arm_mov_imm16(op2);
                const uint32_t value = (high << 16) | low;
                auto it = anchor_mask_by_addr.find(value);
                if (it == anchor_mask_by_addr.end())
                    continue;
                uint32_t entry = find_function_prologue(pc);
                Candidate &c = candidates[entry];
                c.entry = entry;
                c.refs++;
                c.mask |= it->second;
                break;
            }
        }
    }

    std::vector<Candidate> ranked;
    ranked.reserve(candidates.size());
    for (const auto &kv : candidates)
        ranked.push_back(kv.second);
    last_candidate_count = ranked.size();

    auto score = [](const Candidate &c) {
        uint32_t mask = c.mask;
        int bits = 0;
        while (mask) {
            bits += (mask & 1u);
            mask >>= 1;
        }
        uint32_t core_mask = 0;
        for (size_t i = 0; i < kAnchors.size(); i++) {
            if (kAnchors[i].core)
                core_mask |= (1u << i);
        }
        const bool has_core = (c.mask & core_mask) != 0;
        return static_cast<int>(c.refs) * 16 + bits * 8 + (has_core ? 32 : 0);
    };

    std::sort(ranked.begin(), ranked.end(), [&](const Candidate &a, const Candidate &b) {
        int sa = score(a), sb = score(b);
        if (sa != sb)
            return sa > sb;
        return a.entry < b.entry;
    });

    int installed = 0;
    const size_t max_hooks = std::min<size_t>(32, ranked.size());
    for (size_t i = 0; i < max_hooks; i++) {
        const Candidate &c = ranked[i];
        if (!set_exec_breakpoint(c.entry, true))
            continue;
        hook_addrs.insert(c.entry);
        hook_anchor_mask[c.entry] = c.mask;
        installed++;
    }

    hooks_installed = !hook_addrs.empty();
    if (installed > 0)
        gui_nlog_printf("nlog: installed %d scan-derived ARM hook breakpoint(s).\n", installed);
    else
        gui_nlog_printf("nlog: found %zu anchors, %zu candidate functions, installed 0 scan hooks.\n",
                        last_anchor_count, last_candidate_count);
    return installed;
}

} // namespace

void nspire_log_hook_poll(uint32_t pc)
{
    check_config_once();
    if (!hook_enabled || !emulate_cx2)
        return;

    if (pc < 0x10000000u || pc >= 0x14000000u)
        return;

    poll_counter++;
    if (filter_bypass_enabled && !filter_bypass_installed && (poll_counter % kFastPollInterval == 0u))
        (void)apply_filter_bypass_patch(false);

    if (poll_counter % kFastPollInterval == 0u)
        (void)install_known_dispatch_hooks(false);

    if (hooks_installed || !auto_scan_fallback)
        return;

    if (poll_counter % kSlowScanPollInterval == 0u)
        (void)scan_and_install_hooks();
}

bool nspire_log_hook_handle_exec(uint32_t pc)
{
    if (!hook_enabled || !hooks_installed)
        return false;
    pc &= ~3u;
    if (hook_addrs.find(pc) == hook_addrs.end())
        return false;
    total_hook_hits++;
    capture_register_strings(pc);
    return true;
}

void nspire_log_hook_on_memory_write(uint32_t addr, uint32_t size)
{
    check_config_once();
    if (!emulate_cx2 || !mem_areas[1].ptr || size == 0)
        return;

    /* If dispatch hooks are already installed and bypass is settled,
     * write-time discovery can stay idle. */
    if (hooks_installed && (!filter_bypass_enabled || filter_bypass_installed))
        return;

    /* Keep runtime overhead low: probe only aligned word writes and sample
     * a subset while code images are still being loaded. */
    if (size < 4u || (addr & 3u))
        return;
    if ((++dispatch_write_probe_counter & 0x7u) != 0u)
        return;

    const uint32_t base = mem_areas[1].base;
    const uint32_t end = base + mem_areas[1].size;
    const uint32_t last = addr + size - 1u;
    if (last < addr)
        return;

    uint32_t word = addr & ~3u;
    const uint32_t stop = last & ~3u;
    for (;;) {
        if (word >= base && word + 4u <= end) {
            uint32_t op = 0;
            if (read_word_mapped(word, &op) && is_arm_push_prologue(op))
                (void)try_register_dispatch_entry(word, true);
        }
        if (word == stop)
            break;
        word += 4u;
    }

    if (filter_bypass_enabled && !filter_bypass_installed && hook_enabled
            && !dispatch_hook_addrs.empty()) {
        (void)apply_filter_bypass_patch(false);
    }
}

void nspire_log_hook_reset(void)
{
    remove_filter_bypass_patch(false);
    for (uint32_t addr : hook_addrs)
        (void)set_exec_breakpoint(addr, false);
    hook_addrs.clear();
    dispatch_hook_addrs.clear();
    hook_anchor_mask.clear();
    hooks_installed = false;
    dispatch_scan_attempted = false;
    dispatch_last_full_scan_poll = 0;
    dispatch_write_probe_counter = 0;
    scan_attempted = false;
    poll_counter = 0;
    last_anchor_count = 0;
    last_candidate_count = 0;
    total_hook_hits = 0;
    total_lines_emitted = 0;
    hook_hits_by_pc.clear();
    current_file.clear();
    last_file_for_pc.clear();
    last_emitted_line.clear();
}

void nspire_log_hook_scan_now(void)
{
    check_config_once();
    if (!hook_enabled) {
        gui_nlog_printf("nlog: disabled.\n");
        return;
    }
    nspire_log_hook_reset();
    if (filter_bypass_enabled)
        (void)apply_filter_bypass_patch(false);
    int known = install_known_dispatch_hooks(false);
    int scanned = scan_and_install_hooks();
    if ((known + scanned) <= 0)
        gui_nlog_printf("nlog: no hook candidates installed yet.\n");
}

void nspire_log_hook_set_enabled(bool enabled)
{
    check_config_once();
    hook_enabled = enabled;
    if (!enabled) {
        nspire_log_hook_reset();
    } else {
        if (filter_bypass_enabled)
            (void)apply_filter_bypass_patch(true);
        (void)install_known_dispatch_hooks(true);
    }
}

bool nspire_log_hook_is_enabled(void)
{
    check_config_once();
    return hook_enabled;
}

void nspire_log_hook_status(void)
{
    check_config_once();
    size_t dispatch_installed = 0;
    for (uint32_t addr : dispatch_hook_addrs) {
        if (hook_addrs.find(addr) != hook_addrs.end())
            dispatch_installed++;
    }
    gui_nlog_printf("nlog: enabled=%s installed=%s hooks=%zu scanned=%s\n",
                    hook_enabled ? "yes" : "no",
                    hooks_installed ? "yes" : "no",
                    hook_addrs.size(),
                    scan_attempted ? "yes" : "no");
    gui_nlog_printf("nlog: bypass enabled=%s installed=%s\n",
                    filter_bypass_enabled ? "yes" : "no",
                    filter_bypass_installed ? "yes" : "no");
    gui_nlog_printf("nlog: dispatch_hooks=%zu installed_dispatch=%zu discovery=%s autoscan=%s\n",
                    dispatch_hook_addrs.size(),
                    dispatch_installed,
                    dispatch_scan_attempted ? "yes" : "no",
                    auto_scan_fallback ? "on" : "off");
    gui_nlog_printf("nlog: last_scan anchors=%zu candidates=%zu\n",
                    last_anchor_count, last_candidate_count);
    gui_nlog_printf("nlog: hits=%" PRIu64 " emitted_lines=%" PRIu64 "\n",
                    total_hook_hits, total_lines_emitted);
}

void nspire_log_hook_set_filter_bypass(bool enabled)
{
    check_config_once();
    filter_bypass_enabled = enabled;
    if (!enabled) {
        remove_filter_bypass_patch(true);
    } else if (hook_enabled) {
        (void)apply_filter_bypass_patch(true);
    } else {
        gui_nlog_printf("nlog: bypass armed (enable nlog to apply).\n");
    }
}

bool nspire_log_hook_filter_bypass_is_enabled(void)
{
    check_config_once();
    return filter_bypass_enabled;
}

bool nspire_log_hook_filter_bypass_is_installed(void)
{
    check_config_once();
    return filter_bypass_installed;
}
