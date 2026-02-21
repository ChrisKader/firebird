/*
 * TODO:
 * - Explicitely support the endianness (set/get_registers). Currently the host must be little-endian
 *   as ARM is.
 * - fix vFile commands, currently broken because of the armsnippets
 */

/*
 * Some parts derive from GDB's sparc-stub.c.
 * Refer to Appendix D - GDB Remote Serial Protocol in GDB's documentation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#endif

#include "emu.h"
#include "debug.h"
#include "memory/mem.h"
#include "cpu.h"
#include "armsnippets.h"
#include "gdbstub.h"
#include "translate.h"

static void gdbstub_disconnect(void);

bool ndls_is_installed(void) {
    uint32_t *vectors = virt_mem_ptr(0x20, 0x20);
    if (vectors) {
        // The Ndless marker is 8 bytes before the SWI handler
        uint32_t *sig = virt_mem_ptr(vectors[EX_SWI] - 8, 4);
        if (sig) return *sig == 0x4E455854 /* 'NEXT' */;
    }
    return false;
}

static int listen_socket_fd = -1;
static int socket_fd = -1;
static bool gdb_handshake_complete = false;

static const char gdb_target_xml[] =
    "<?xml version=\"1.0\"?>"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target>"
    "<architecture>arm</architecture>"
    "<osabi>none</osabi>"
    "<feature name=\"org.gnu.gdb.arm.core\">"
    "<reg name=\"r0\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r1\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r2\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r3\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r4\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r5\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r6\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r7\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r8\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r9\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r10\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r11\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"r12\" bitsize=\"32\" type=\"uint32\"/>"
    "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
    "<reg name=\"lr\" bitsize=\"32\" type=\"code_ptr\"/>"
    "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
    "<reg name=\"cpsr\" bitsize=\"32\" type=\"uint32\"/>"
    "</feature>"
    "</target>";

static char *gdb_memory_map_buf = NULL;
static size_t gdb_memory_map_cap = 0;
static char *gdb_fb_map_buf = NULL;
static size_t gdb_fb_map_cap = 0;

enum gdb_local_action {
    GDB_LOCAL_NONE = 0,
    GDB_LOCAL_CONTINUE = 1,
    GDB_LOCAL_STEP = 2,
};

static volatile int gdb_local_action = GDB_LOCAL_NONE;
static volatile bool gdb_allow_local_interrupt = false;
static volatile bool gdb_waiting_for_attach = false;

#define GDB_HOSTIO_MAX_FDS 16

struct gdb_hostio_fd {
    int fd;
    bool used;
};

static struct gdb_hostio_fd gdb_hostio_fds[GDB_HOSTIO_MAX_FDS];

static void gdb_hostio_reset_fds(void)
{
    for (size_t i = 0; i < GDB_HOSTIO_MAX_FDS; ++i)
        gdb_hostio_fds[i].used = false;
}

bool gdbstub_is_listening(void)
{
    return listen_socket_fd != -1;
}

void gdbstub_set_waiting_for_attach(bool waiting)
{
    gdb_waiting_for_attach = waiting;
}

static void log_socket_error(const char *msg) {
#ifdef __MINGW32__
    int errCode = WSAGetLastError();
    LPSTR errString = NULL;  // will be allocated and filled by FormatMessage
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  0, errCode, 0, (LPSTR)&errString, 0, 0);
    gui_debug_printf("%s: %s (%i)\n", msg, errString, errCode);
    LocalFree( errString );
#else
    gui_perror(msg);
#endif
}

static char sockbuf[4096];
static char *sockbufptr = sockbuf;

static bool flush_out_buffer(void) {
#ifndef MSG_NOSIGNAL
    #ifdef __APPLE__
        #define MSG_NOSIGNAL SO_NOSIGPIPE
    #else
        #define MSG_NOSIGNAL 0
    #endif
#endif
    char *p = sockbuf;
    while (p != sockbufptr) {
        int n = send(socket_fd, p, sockbufptr-p, MSG_NOSIGNAL);
        if (n == -1) {
#ifdef __MINGW32__
            if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
            if (errno == EAGAIN)
#endif
                continue; // not ready to send
            else {
                log_socket_error("Failed to send to GDB stub socket");
                return false;
            }
        }
        if (n == 0) {
            log_socket_error("GDB socket closed while sending");
            return false;
        }
        p += n;
    }
    sockbufptr = sockbuf;
    return true;
}

static bool put_debug_char(char c) {
    if (log_enabled[LOG_GDB]) {
        logprintf(LOG_GDB, "%c", c);
        fflush(stdout);
        if (c == '+' || c == '-')
            logprintf(LOG_GDB, "\t");
    }
    if (sockbufptr == sockbuf + sizeof sockbuf)
        if(!flush_out_buffer())
            return false;

    *sockbufptr++ = c;
    return true;
}

// returns 1 if at least one instruction translated in the range
static int range_translated(uint32_t range_start, uint32_t range_end) {
    uint32_t pc;
    int translated = 0;
    for (pc = range_start; pc < range_end;  pc += 4) {
        void *pc_ram_ptr = virt_mem_ptr(pc, 4);
        if (!pc_ram_ptr)
            break;
        translated |= RAM_FLAGS(pc_ram_ptr) & RF_CODE_TRANSLATED;
    }
    return translated;
}

// returns 0 on timeout, 1 if ready (or EOF/disconnected!) and -1 on error.
static int can_read_from_socket(int socket_fd) {
    const int timeoutms = 100;

    // There are reports that WSAPoll times out on some exceptions,
    // so use select. Would be possible on other platforms as well,
    // but it's limited to fds < FD_SETSIZE, unlike poll.
#ifdef __MINGW32__
    fd_set rfds;
    FD_ZERO(&rfds);
    // No need to worry about FD_SETSIZE, winsock2 uses a list instead
    FD_SET(socket_fd, &rfds);
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = timeoutms * 1000,
    };
    return select(socket_fd + 1, &rfds, NULL, NULL, &timeout);
#else
    struct pollfd pfd;
    pfd.fd = socket_fd;
    pfd.events = POLLIN;
    return poll(&pfd, 1, timeoutms);
#endif
}

/* Returns -1 on disconnection */
static char get_debug_char(void) {
    while(true)
    {
        int p = can_read_from_socket(socket_fd);
        if(p == -1) {
            log_socket_error("Failed to poll socket");
            return -1;
        }

        if(p) // Data available
            break;

        else // No data available
        {
            if(exiting)
                return -1;

            if (gdb_allow_local_interrupt && gdb_local_action != GDB_LOCAL_NONE)
                return (char)-2;

            gui_do_stuff(false);
        }
    }

    char c;
    int r = recv(socket_fd, &c, 1, 0);
    if (r == -1) {
        // only for debugging - log_socket_error("Failed to recv from GDB stub socket");
        return -1;
    }
    if (r == 0)
        return -1; // disconnected
    if (log_enabled[LOG_GDB]) {
        logprintf(LOG_GDB, "%c", c);
        fflush(stdout);
        if (c == '+' || c == '-')
            logprintf(LOG_GDB, "\n");
    }
    return c;
}

static void set_nonblocking(int socket, bool nonblocking) {
#ifdef __MINGW32__
    u_long mode = nonblocking;
    ioctlsocket(socket, FIONBIO, &mode);
#else
    int ret = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, nonblocking ? (ret | O_NONBLOCK) : (ret & ~O_NONBLOCK));
    ret = fcntl(socket, F_GETFD, 0);
    fcntl(socket, F_SETFD, ret | FD_CLOEXEC);
#endif
}

bool gdbstub_init(unsigned int port) {
    struct sockaddr_in sockaddr;
    int r;

#ifdef __MINGW32__
    WORD wVersionRequested = MAKEWORD(2, 0);
    WSADATA wsaData;
    if (WSAStartup(wVersionRequested, &wsaData)) {
        log_socket_error("WSAStartup failed");
        return false;
    }
#endif

    listen_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_socket_fd == -1) {
        log_socket_error("Failed to create GDB stub socket");
        return false;
    }
    set_nonblocking(listen_socket_fd, true);

    memset (&sockaddr, '\000', sizeof sockaddr);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    r = bind(listen_socket_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (r == -1) {
        log_socket_error("Failed to bind GDB stub socket. Check that Firebird is not already running");
    #ifdef __MINGW32__
        closesocket(listen_socket_fd);
    #else
        close(listen_socket_fd);
    #endif
        listen_socket_fd = -1;
        return false;
    }
    r = listen(listen_socket_fd, 0);
    if (r == -1) {
        log_socket_error("Failed to listen on GDB stub socket");
    #ifdef __MINGW32__
        closesocket(listen_socket_fd);
    #else
        close(listen_socket_fd);
    #endif
        listen_socket_fd = -1;
        return false;
    }

    return true;
}

// program block pre-allocated by Ndless, used for vOffsets queries
static uint32_t ndls_debug_alloc_block = 0;
static bool ndls_debug_received = false;

static void gdb_connect_ndls_cb(struct arm_state *state) {
    ndls_debug_alloc_block = state->reg[0]; // can be 0
    ndls_debug_received = true;
    if (!ndls_debug_alloc_block)
        gui_debug_printf("Ndless failed to allocate the memory block for application debugging.\n");
}

/* Initial buffer size for GDB packets; will grow on demand. */
#define GDB_INITIAL_BUF 2048
/* Cap payload size to avoid runaway allocations. */
#define GDB_MAX_PACKET_PAYLOAD (64u * 1024u * 1024u)
#define GDB_MAX_PACKET_BYTES (GDB_MAX_PACKET_PAYLOAD + 1u)
/* Keep console output packets modest. */
#define GDB_CONSOLE_CHUNK 1024u

static const char hexchars[]="0123456789abcdef";

enum regnames {R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, SP, LR, PC,
               F0H, F0M, F0L,
               F1H, F1M, F1L,
               F2H, F2M, F2L,
               F3H, F3M, F3L,
               F4H, F4M, F4L,
               F5H, F5M, F5L,
               F6H, F6M, F6L,
               F7H, F7M, F7L, FPS, CPSR, NUMREGS};

/* Number of bytes of registers. */
#define NUMREGBYTES (NUMREGS * 4)

// see GDB's include/gdb/signals.h
enum target_signal {SIGNAL_ILL_INSTR = 4, SIGNAL_TRAP = 5};

/* Convert ch from a hex digit to an int */
static int hex(char ch) {
    if (ch >= 'a' && ch <= 'f')
        return ch-'a'+10;
    if (ch >= '0' && ch <= '9')
        return ch-'0';
    if (ch >= 'A' && ch <= 'F')
        return ch-'A'+10;
    return -1;
}

static char *remcomInBuffer = NULL;
static size_t remcomInCapacity = 0;
static char *remcomOutBuffer = NULL;
static size_t remcomOutCapacity = 0;

static bool gdb_reserve_buffer(char **buf, size_t *cap, size_t needed)
{
    if (needed == 0)
        needed = 1;
    if (needed > GDB_MAX_PACKET_BYTES)
        return false;
    if (*cap >= needed)
        return true;
    size_t newcap = *cap ? *cap : GDB_INITIAL_BUF;
    while (newcap < needed) {
        if (newcap > GDB_MAX_PACKET_BYTES / 2) {
            newcap = GDB_MAX_PACKET_BYTES;
            break;
        }
        newcap *= 2;
    }
    if (newcap < needed)
        return false;
    char *newbuf = realloc(*buf, newcap);
    if (!newbuf)
        return false;
    *buf = newbuf;
    *cap = newcap;
    return true;
}

static bool gdb_ensure_in_buffer(size_t needed)
{
    return gdb_reserve_buffer(&remcomInBuffer, &remcomInCapacity, needed);
}

static bool gdb_ensure_out_buffer(size_t needed)
{
    return gdb_reserve_buffer(&remcomOutBuffer, &remcomOutCapacity, needed);
}

static bool gdb_build_memory_map(size_t *out_len)
{
    size_t needed = memory_build_gdb_map(NULL, 0);
    if (!gdb_reserve_buffer(&gdb_memory_map_buf, &gdb_memory_map_cap, needed + 1))
        return false;
    size_t len = memory_build_gdb_map(gdb_memory_map_buf, gdb_memory_map_cap);
    if (out_len)
        *out_len = len;
    return true;
}

static bool gdb_build_fb_map(size_t *out_len)
{
    size_t needed = memory_build_fb_map(NULL, 0);
    if (!gdb_reserve_buffer(&gdb_fb_map_buf, &gdb_fb_map_cap, needed + 1))
        return false;
    size_t len = memory_build_fb_map(gdb_fb_map_buf, gdb_fb_map_cap);
    if (out_len)
        *out_len = len;
    return true;
}

static bool gdb_reply_memory_region_info(uint32_t addr)
{
    struct memory_region_info info;
    if (!memory_query_region(addr, &info))
        return false;
    if (!gdb_ensure_out_buffer(96))
        return false;
    snprintf(remcomOutBuffer, remcomOutCapacity,
             "start:%x;size:%x;permissions:%s;",
             info.start, info.size, info.perm);
    return true;
}

static bool gdb_hostio_fd_valid(int fd)
{
    for (size_t i = 0; i < GDB_HOSTIO_MAX_FDS; ++i) {
        if (gdb_hostio_fds[i].used && gdb_hostio_fds[i].fd == fd)
            return true;
    }
    return false;
}

static bool gdb_hostio_fd_add(int fd)
{
    for (size_t i = 0; i < GDB_HOSTIO_MAX_FDS; ++i) {
        if (!gdb_hostio_fds[i].used) {
            gdb_hostio_fds[i].used = true;
            gdb_hostio_fds[i].fd = fd;
            return true;
        }
    }
    return false;
}

static void gdb_hostio_fd_remove(int fd)
{
    for (size_t i = 0; i < GDB_HOSTIO_MAX_FDS; ++i) {
        if (gdb_hostio_fds[i].used && gdb_hostio_fds[i].fd == fd) {
            gdb_hostio_fds[i].used = false;
            return;
        }
    }
}

enum gdb_fileio_error {
    GDB_FILEIO_SUCCESS = 0,
    GDB_FILEIO_EPERM = 1,
    GDB_FILEIO_ENOENT = 2,
    GDB_FILEIO_EINTR = 4,
    GDB_FILEIO_EIO = 5,
    GDB_FILEIO_EBADF = 9,
    GDB_FILEIO_EACCES = 13,
    GDB_FILEIO_EFAULT = 14,
    GDB_FILEIO_EBUSY = 16,
    GDB_FILEIO_EEXIST = 17,
    GDB_FILEIO_ENODEV = 19,
    GDB_FILEIO_ENOTDIR = 20,
    GDB_FILEIO_EISDIR = 21,
    GDB_FILEIO_EINVAL = 22,
    GDB_FILEIO_ENFILE = 23,
    GDB_FILEIO_EMFILE = 24,
    GDB_FILEIO_EFBIG = 27,
    GDB_FILEIO_ENOSPC = 28,
    GDB_FILEIO_ESPIPE = 29,
    GDB_FILEIO_EROFS = 30,
    GDB_FILEIO_ENOSYS = 88,
    GDB_FILEIO_ENAMETOOLONG = 91,
    GDB_FILEIO_EUNKNOWN = 9999,
};

static int gdb_hostio_error_from_errno(int err)
{
    switch (err) {
        case EPERM: return GDB_FILEIO_EPERM;
        case ENOENT: return GDB_FILEIO_ENOENT;
        case EINTR: return GDB_FILEIO_EINTR;
        case EIO: return GDB_FILEIO_EIO;
        case EBADF: return GDB_FILEIO_EBADF;
        case EACCES: return GDB_FILEIO_EACCES;
        case EFAULT: return GDB_FILEIO_EFAULT;
        case EBUSY: return GDB_FILEIO_EBUSY;
        case EEXIST: return GDB_FILEIO_EEXIST;
        case ENODEV: return GDB_FILEIO_ENODEV;
        case ENOTDIR: return GDB_FILEIO_ENOTDIR;
        case EISDIR: return GDB_FILEIO_EISDIR;
        case EINVAL: return GDB_FILEIO_EINVAL;
        case ENFILE: return GDB_FILEIO_ENFILE;
        case EMFILE: return GDB_FILEIO_EMFILE;
        case EFBIG: return GDB_FILEIO_EFBIG;
        case ENOSPC: return GDB_FILEIO_ENOSPC;
        case ESPIPE: return GDB_FILEIO_ESPIPE;
        case EROFS: return GDB_FILEIO_EROFS;
        case ENOSYS: return GDB_FILEIO_ENOSYS;
        case ENAMETOOLONG: return GDB_FILEIO_ENAMETOOLONG;
        default: return GDB_FILEIO_EUNKNOWN;
    }
}

static void gdb_hostio_reply_error(void)
{
    snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", gdb_hostio_error_from_errno(errno));
}

static void gdb_hostio_reply_value(int result)
{
    snprintf(remcomOutBuffer, remcomOutCapacity, "F%x", result);
}

static bool gdb_hostio_parse_hex_int(const char **pp, long long *value)
{
    const char *p = *pp;
    long long result = 0;
    int digits = 0;
    while (*p && *p != ',') {
        int h = hex(*p);
        if (h < 0)
            return false;
        result = (result << 4) | h;
        ++p;
        ++digits;
        if (digits > 16)
            return false;
    }
    if (digits == 0)
        return false;
    *pp = p;
    *value = result;
    return true;
}

static bool gdb_hostio_parse_hex_path(const char **pp, char *out, size_t out_size)
{
    const char *p = *pp;
    size_t count = 0;
    while (*p && *p != ',') {
        int h1 = hex(p[0]);
        int h2 = hex(p[1]);
        if (h1 < 0 || h2 < 0)
            return false;
        if (count + 1 >= out_size)
            return false;
        out[count++] = (char)((h1 << 4) | h2);
        p += 2;
    }
    out[count] = 0;
    *pp = p;
    return true;
}

static bool gdb_hostio_skip_comma(const char **pp)
{
    if (**pp == ',') {
        ++(*pp);
        return true;
    }
    return false;
}

static void gdb_hostio_pack_be(long long num, char *buf, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        buf[i] = (char)((num >> (8 * (bytes - i - 1))) & 0xff);
}

struct gdb_fio_stat
{
    char fst_dev[4];
    char fst_ino[4];
    char fst_mode[4];
    char fst_nlink[4];
    char fst_uid[4];
    char fst_gid[4];
    char fst_rdev[4];
    char fst_size[8];
    char fst_blksize[8];
    char fst_blocks[8];
    char fst_atime[4];
    char fst_mtime[4];
    char fst_ctime[4];
};

static void gdb_hostio_pack_stat(const struct stat *st, struct gdb_fio_stat *fst)
{
    uint32_t mode = 0;
    if (S_ISREG(st->st_mode))
        mode |= 0100000;
    else if (S_ISDIR(st->st_mode))
        mode |= 0040000;
    else if (S_ISCHR(st->st_mode))
        mode |= 0020000;
    if (st->st_mode & S_IRUSR) mode |= 0400;
    if (st->st_mode & S_IWUSR) mode |= 0200;
    if (st->st_mode & S_IXUSR) mode |= 0100;
    if (st->st_mode & S_IRGRP) mode |= 0040;
    if (st->st_mode & S_IWGRP) mode |= 0020;
    if (st->st_mode & S_IXGRP) mode |= 0010;
    if (st->st_mode & S_IROTH) mode |= 0004;
    if (st->st_mode & S_IWOTH) mode |= 0002;
    if (st->st_mode & S_IXOTH) mode |= 0001;

    gdb_hostio_pack_be((long long)st->st_dev, fst->fst_dev, 4);
    gdb_hostio_pack_be((long long)st->st_ino, fst->fst_ino, 4);
    gdb_hostio_pack_be((long long)mode, fst->fst_mode, 4);
    gdb_hostio_pack_be((long long)st->st_nlink, fst->fst_nlink, 4);
    gdb_hostio_pack_be((long long)st->st_uid, fst->fst_uid, 4);
    gdb_hostio_pack_be((long long)st->st_gid, fst->fst_gid, 4);
    gdb_hostio_pack_be((long long)st->st_rdev, fst->fst_rdev, 4);
    gdb_hostio_pack_be((long long)st->st_size, fst->fst_size, 8);
#ifdef __APPLE__
    gdb_hostio_pack_be((long long)st->st_blksize, fst->fst_blksize, 8);
    gdb_hostio_pack_be((long long)st->st_blocks, fst->fst_blocks, 8);
    gdb_hostio_pack_be((long long)st->st_atimespec.tv_sec, fst->fst_atime, 4);
    gdb_hostio_pack_be((long long)st->st_mtimespec.tv_sec, fst->fst_mtime, 4);
    gdb_hostio_pack_be((long long)st->st_ctimespec.tv_sec, fst->fst_ctime, 4);
#else
    gdb_hostio_pack_be((long long)st->st_blksize, fst->fst_blksize, 8);
    gdb_hostio_pack_be((long long)st->st_blocks, fst->fst_blocks, 8);
    gdb_hostio_pack_be((long long)st->st_atim.tv_sec, fst->fst_atime, 4);
    gdb_hostio_pack_be((long long)st->st_mtim.tv_sec, fst->fst_mtime, 4);
    gdb_hostio_pack_be((long long)st->st_ctim.tv_sec, fst->fst_ctime, 4);
#endif
}

static bool gdb_hostio_reply_with_data(const char *data, size_t len)
{
    char header[32];
    int header_len = snprintf(header, sizeof(header), "F%zx;", len);
    size_t max_payload = GDB_MAX_PACKET_PAYLOAD;
    if ((size_t)header_len >= max_payload)
        return false;

    size_t worst = (size_t)header_len + len * 2 + 1;
    if (worst > max_payload)
        return false;
    if (!gdb_ensure_out_buffer(worst))
        return false;

    memcpy(remcomOutBuffer, header, (size_t)header_len);
    size_t out = (size_t)header_len;
    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)data[i];
        if (b == '$' || b == '#' || b == '}' || b == '*') {
            remcomOutBuffer[out++] = '}';
            remcomOutBuffer[out++] = (char)(b ^ 0x20);
        } else {
            remcomOutBuffer[out++] = (char)b;
        }
    }
    remcomOutBuffer[out] = 0;
    return true;
}

static bool gdb_hostio_handle_vfile(const char *cmd, int *out_len)
{
    if (strncmp(cmd, "File:", 5) != 0)
        return false;

    const char *p = cmd + 5;
    if (!strncmp(p, "setfs:", 6)) {
        remcomOutBuffer[0] = 0;
        return true;
    }

    if (!strncmp(p, "open:", 5)) {
        char path_buf[1024];
        long long flags = 0;
        long long mode = 0;
        p += 5;
        if (!gdb_hostio_parse_hex_path(&p, path_buf, sizeof(path_buf))
            || !gdb_hostio_skip_comma(&p)
            || !gdb_hostio_parse_hex_int(&p, &flags)
            || !gdb_hostio_skip_comma(&p)
            || !gdb_hostio_parse_hex_int(&p, &mode)
            || *p != 0) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            return true;
        }

        int open_flags = 0;
        if (flags & 0x1)
            open_flags |= O_WRONLY;
        else if (flags & 0x2)
            open_flags |= O_RDWR;
        else
            open_flags |= O_RDONLY;
        if (flags & 0x8)
            open_flags |= O_APPEND;
        if (flags & 0x200)
            open_flags |= O_CREAT;
        if (flags & 0x400)
            open_flags |= O_TRUNC;
        if (flags & 0x800)
            open_flags |= O_EXCL;

        int fd = open(path_buf, open_flags, (mode_t)mode);
        if (fd == -1) {
            gdb_hostio_reply_error();
            return true;
        }
        if (!gdb_hostio_fd_add(fd)) {
            close(fd);
            errno = ENFILE;
            gdb_hostio_reply_error();
            return true;
        }
        gdb_hostio_reply_value(fd);
        return true;
    }

    if (!strncmp(p, "close:", 6)) {
        long long fd = 0;
        p += 6;
        if (!gdb_hostio_parse_hex_int(&p, &fd) || *p != 0 || !gdb_hostio_fd_valid((int)fd)) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EBADF);
            return true;
        }
        if (close((int)fd) == -1) {
            gdb_hostio_reply_error();
            return true;
        }
        gdb_hostio_fd_remove((int)fd);
        gdb_hostio_reply_value(0);
        return true;
    }

    if (!strncmp(p, "pread:", 6)) {
        long long fd = 0;
        long long len = 0;
        long long off = 0;
        p += 6;
        if (!gdb_hostio_parse_hex_int(&p, &fd)
            || !gdb_hostio_skip_comma(&p)
            || !gdb_hostio_fd_valid((int)fd)
            || !gdb_hostio_parse_hex_int(&p, &len)
            || !gdb_hostio_skip_comma(&p)
            || !gdb_hostio_parse_hex_int(&p, &off)
            || *p != 0) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            return true;
        }

        if (len < 0)
            len = 0;
        size_t header_len = 18;
        size_t max_data = (GDB_MAX_PACKET_PAYLOAD > header_len)
                              ? (GDB_MAX_PACKET_PAYLOAD - header_len) / 2
                              : 0;
        if ((size_t)len > max_data)
            len = (long long)max_data;

        char *data = (char *)malloc((size_t)len);
        if (!data) {
            errno = ENOMEM;
            gdb_hostio_reply_error();
            return true;
        }
#ifdef HAVE_PREAD
        ssize_t ret = pread((int)fd, data, (size_t)len, (off_t)off);
#else
        ssize_t ret = -1;
#endif
        if (ret == -1) {
            if (lseek((int)fd, (off_t)off, SEEK_SET) != -1)
                ret = read((int)fd, data, (size_t)len);
        }

        if (ret == -1) {
            gdb_hostio_reply_error();
            free(data);
            return true;
        }

        if (ret == 0) {
            gdb_hostio_reply_value(0);
            free(data);
            return true;
        }

        if (!gdb_hostio_reply_with_data(data, (size_t)ret)) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            free(data);
            return true;
        }
        free(data);
        if (out_len)
            *out_len = (int)strlen(remcomOutBuffer);
        return true;
    }

    if (!strncmp(p, "fstat:", 6)) {
        long long fd = 0;
        p += 6;
        if (!gdb_hostio_parse_hex_int(&p, &fd) || *p != 0 || !gdb_hostio_fd_valid((int)fd)) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EBADF);
            return true;
        }
        struct stat st;
        if (fstat((int)fd, &st) == -1) {
            gdb_hostio_reply_error();
            return true;
        }
        struct gdb_fio_stat fst;
        gdb_hostio_pack_stat(&st, &fst);
        if (!gdb_hostio_reply_with_data((const char *)&fst, sizeof(fst))) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            return true;
        }
        if (out_len)
            *out_len = (int)strlen(remcomOutBuffer);
        return true;
    }

    if (!strncmp(p, "stat:", 5)) {
        char path_buf[1024];
        p += 5;
        if (!gdb_hostio_parse_hex_path(&p, path_buf, sizeof(path_buf)) || *p != 0) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            return true;
        }
        struct stat st;
        if (stat(path_buf, &st) == -1) {
            gdb_hostio_reply_error();
            return true;
        }
        struct gdb_fio_stat fst;
        gdb_hostio_pack_stat(&st, &fst);
        if (!gdb_hostio_reply_with_data((const char *)&fst, sizeof(fst))) {
            snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_EINVAL);
            return true;
        }
        if (out_len)
            *out_len = (int)strlen(remcomOutBuffer);
        return true;
    }

    snprintf(remcomOutBuffer, remcomOutCapacity, "F-1,%x", GDB_FILEIO_ENOSYS);
    return true;
}

static int gdb_take_local_action(void)
{
    int action = gdb_local_action;
    gdb_local_action = GDB_LOCAL_NONE;
    return action;
}

static char *gdb_local_packet(void)
{
    int action = gdb_take_local_action();
    if (action == GDB_LOCAL_CONTINUE) {
        remcomInBuffer[0] = 'c';
        remcomInBuffer[1] = 0;
        return remcomInBuffer;
    }
    if (action == GDB_LOCAL_STEP) {
        remcomInBuffer[0] = 's';
        remcomInBuffer[1] = 0;
        return remcomInBuffer;
    }
    remcomInBuffer[0] = 0;
    return remcomInBuffer;
}

static bool gdb_match_token(const char *cmd, const char *token)
{
    size_t i = 0;
    for (; token[i]; ++i) {
        if (!cmd[i])
            return false;
        if ((char)tolower((unsigned char)cmd[i]) != token[i])
            return false;
    }
    return cmd[i] == 0 || isspace((unsigned char)cmd[i]);
}

bool gdbstub_queue_local_command(const char *cmd)
{
    if (!cmd || !gdb_connected)
        return false;
    while (*cmd && isspace((unsigned char)*cmd))
        ++cmd;
    if (!*cmd)
        return false;
    if ((cmd[0] == 'c' || cmd[0] == 'C') && (cmd[1] == 0 || isspace((unsigned char)cmd[1])))
        gdb_local_action = GDB_LOCAL_CONTINUE;
    else if ((cmd[0] == 's' || cmd[0] == 'S') && (cmd[1] == 0 || isspace((unsigned char)cmd[1])))
        gdb_local_action = GDB_LOCAL_STEP;
    else if (gdb_match_token(cmd, "continue"))
        gdb_local_action = GDB_LOCAL_CONTINUE;
    else if (gdb_match_token(cmd, "step"))
        gdb_local_action = GDB_LOCAL_STEP;
    else
        return false;
    return true;
}

/* scan for the sequence $<data>#<checksum>. # will be replaced with \0.
 * Returns NULL on disconnection. */
char *getpacket(void) {
    if (!gdb_ensure_in_buffer(GDB_INITIAL_BUF))
        return NULL;
    char *buffer = remcomInBuffer;
    unsigned char checksum;
    unsigned char xmitcsum;
    int count;
    char ch;

    while (1) {
        gdb_allow_local_interrupt = true;
        /* wait around for the start character, ignore all other characters */
        do {
            ch = get_debug_char();
            if (ch == (char)-1) // disconnected
                return NULL;
            if (ch == (char)-2) { // local command
                return gdb_local_packet();
            }
        } while (ch != '$');

retry:
        checksum = 0;
        count = 0;
        gdb_allow_local_interrupt = false;

        /* now, read until a # or end of buffer is found */
        while (1) {
            ch = get_debug_char();
            if (ch == (char)-1)
                return NULL;
            if (ch == (char)-2) {
                continue;
            }
            if (ch == '$')
                goto retry;
            if (ch == '#')
                break;
            if ((size_t)count + 1 >= remcomInCapacity) {
                if (!gdb_ensure_in_buffer(remcomInCapacity * 2))
                    return NULL;
                buffer = remcomInBuffer;
            }
            buffer[count] = ch;
            count = count + 1;
            checksum = checksum + ch;
        }

        if (ch == '#') {
            buffer[count] = 0;
            ch = get_debug_char();
            if (ch == (char)-1)
                return NULL;
            xmitcsum = hex(ch) << 4;
            ch = get_debug_char();
            if (ch == (char)-1)
                return NULL;
            xmitcsum += hex(ch);

            if (checksum != xmitcsum) {
                if(!put_debug_char('-')	/* failed checksum */
                   || !flush_out_buffer())
                    return NULL;
            } else {
                put_debug_char('+');	/* successful transfer */

                /* if a sequence char is present, reply the sequence ID */
                if(buffer[2] == ':') {
                    if(!put_debug_char(buffer[0])
                       || !put_debug_char(buffer[1])
                       || !flush_out_buffer())
                        return NULL;

                    return &buffer[3];
                }
                if(!flush_out_buffer())
                    return NULL;

                return &buffer[0];
            }
        }
        gdb_allow_local_interrupt = true;
    }
}

/* send the packet in buffer.  */
static bool putpacket(char *buffer) {
    unsigned char checksum;
    int count;
    char ch;

    /*  $<packet info>#<checksum> */
    do {
        if(!put_debug_char('$'))
            return false;

        checksum = 0;
        count = 0;

        while ((ch = buffer[count])) {
            put_debug_char(ch);
            checksum += ch;
            count += 1;
        }

        if(!put_debug_char('#')
           || !put_debug_char(hexchars[checksum >> 4])
           || !put_debug_char(hexchars[checksum & 0xf])
           || !flush_out_buffer())
            return false;

        ch = get_debug_char();
    } while (ch != '+' && ch != (char) -1);

    return true;
}

/* Indicate to caller of mem2hex or hex2mem that there has been an
 * error.  */
static int mem_err = 0;

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null), in case of mem fault,
 * return 0.
 * If MAY_FAULT is non-zero, then we will handle memory faults by returning
 * a 0, else treat a fault like any other fault in the stub.
 */
static char *mem2hex(void *mem, char *buf, int count) {
    unsigned char ch;
    unsigned char *memptr = mem;

    while (count-- > 0) {
        ch = *memptr++;
        if (mem_err)
            return 0;
        *buf++ = hexchars[ch >> 4];
        *buf++ = hexchars[ch & 0xf];
    }
    *buf = 0;
    return buf;
}

static bool gdb_send_console_text(const char *text)
{
    if (!text)
        return true;
    size_t len = strlen(text);
    while (len) {
        size_t chunk = GDB_CONSOLE_CHUNK;
        if (chunk > len)
            chunk = len;
        if (!gdb_ensure_out_buffer(chunk * 2 + 2))
            return false;
        remcomOutBuffer[0] = 'O';
        if (!mem2hex((void *)text, remcomOutBuffer + 1, (int)chunk))
            return false;
        if (!putpacket(remcomOutBuffer))
            return false;
        text += chunk;
        len -= chunk;
    }
    return true;
}

/* convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written.
 * If count is null stops at the first non hex digit */
static void *hex2mem(char *buf, void *mem, int count) {
    int i;
    int ch;
    uint8_t *memb = mem;

    for (i = 0; i < count || !count; i++) {
        ch = hex(*buf++);
        if (ch == -1)
            return memb;
        ch <<= 4;
        ch |= hex(*buf++);
        *memb++ = (uint8_t) ch;
        if (mem_err)
            return 0;
    }
    return memb;
}

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int hexToInt(char **ptr, int *intValue) {
    int numChars = 0;
    int hexValue;

    *intValue = 0;
    while (**ptr) {
        hexValue = hex(**ptr);
        if (hexValue < 0)
            break;
        *intValue = (*intValue << 4) | hexValue;
        numChars ++;
        (*ptr)++;
    }

    return (numChars);
}

/* See Appendix D - GDB Remote Serial Protocol - Overview.
 * A null character is appended. */
__attribute__((unused)) static void binary_escape(char *in, int insize, char *out, int outsize) {
    while (insize-- > 0 && outsize > 1) {
        if (*in == '#' || *in == '$' || *in == '}' || *in == 0x2A) {
            if (outsize < 3)
                break;
            *out++ = '}';
            *out++ = (0x20 ^ *in++);
            outsize -= 2;
        }
        else {
            *out++ = *in++;
            outsize--;
        }
    }
    *out = '\0';
}

/* From emu to GDB. Returns regbuf. */
static uint32_t *get_registers(uint32_t regbuf[NUMREGS]) {
    // GDB's format in arm-tdep.c/arm_register_names
    memset(regbuf, 0, sizeof(uint32_t) * NUMREGS);
    memcpy(regbuf, arm.reg, sizeof(uint32_t) * 16);
    regbuf[NUMREGS-1] = (uint32_t)get_cpsr();
    return regbuf;
}

/* From GDB to emu */
static void set_registers(const uint32_t regbuf[NUMREGS]) {
    memcpy(arm.reg, regbuf, sizeof(uint32_t) * 16);
    set_cpsr_full(regbuf[NUMREGS-1]);
}

/* GDB Host I/O */

#define append_hex_char(ptr,ch) do {*ptr++ = hexchars[(ch) >> 4]; *ptr++ = hexchars[(ch) & 0xf];} while (0)

/* See GDB's documentation: D.3 Stop Reply Packets
 * stop reason and r can be null. */
static bool send_stop_reply(int signal, const char *stop_reason, const char *r) {
    if (!gdb_ensure_out_buffer(128))
        return false;
    char *ptr = remcomOutBuffer;
    ptr += sprintf(ptr, "T%02xthread:1;", signal);
    if (stop_reason) {
        strcpy(ptr, stop_reason);
        ptr += strlen(stop_reason);
        *ptr++ = ':';
        strcpy(ptr, r);
        ptr += strlen(ptr);
        *ptr++ = ';';
    }
    append_hex_char(ptr, 13);
    *ptr++ = ':';
    ptr = mem2hex(&arm.reg[13], ptr, sizeof(uint32_t));
    *ptr++ = ';';
    append_hex_char(ptr, 15);
    *ptr++ = ':';
    ptr = mem2hex(&arm.reg[15], ptr, sizeof(uint32_t));
    *ptr++ = ';';
    *ptr = 0;
    return putpacket(remcomOutBuffer);
}

void gdbstub_loop(void) {
    int addr;
    int length;
    char *ptr, *ptr1;
    void *ramaddr;
    uint32_t regbuf[NUMREGS];
    bool reply, set;

    gui_debugger_entered_or_left(in_debugger = true);

    while (1) {
        if (!gdb_ensure_out_buffer(GDB_INITIAL_BUF))
            goto disconnect;
        remcomOutBuffer[0] = 0;

        ptr = getpacket();
        if (!ptr) {
            gdbstub_disconnect();
            gui_debugger_entered_or_left(in_debugger = false);
            return;
        }
        reply = true;
        switch (*ptr++) {
            case '?':
                if(!send_stop_reply(SIGNAL_TRAP, NULL, 0))
                    goto disconnect;

                reply = false; // already done
                break;

            case 'g':  /* return the value of the CPU registers */
                get_registers(regbuf);
                if (!gdb_ensure_out_buffer(NUMREGBYTES * 2 + 1)) {
                    strcpy(remcomOutBuffer, "E01");
                    break;
                }
                ptr = remcomOutBuffer;
                ptr = mem2hex(regbuf, ptr, NUMREGS * sizeof(uint32_t));
                break;

            case 'G':  /* set the value of the CPU registers - return OK */
                hex2mem(ptr, regbuf, NUMREGS * sizeof(uint32_t));
                set_registers(regbuf);
                strcpy(remcomOutBuffer,"OK");
                break;

            case 'H':
                if(ptr[1] == '1')
                    strcpy(remcomOutBuffer, "OK");
                break;

            case 'p': /* pn Read the value of register n */
                if (hexToInt(&ptr, &addr) && (size_t)addr < sizeof(regbuf)/sizeof(uint32_t)) {
                    if (!gdb_ensure_out_buffer(8 + 1)) {
                        strcpy(remcomOutBuffer, "E01");
                        break;
                    }
                    mem2hex(get_registers(regbuf) + addr, remcomOutBuffer, sizeof(uint32_t));
                } else {
                    strcpy(remcomOutBuffer,"E01");
                }
                break;

            case 'P': /* Pn=r Write register n with value r */
                ptr = strtok(ptr, "=");
                if (hexToInt(&ptr, &addr)
                        && (ptr=strtok(NULL, ""))
                        && (size_t)addr < sizeof(regbuf)/sizeof(uint32_t)
                        // TODO hex2mem doesn't check the format
                        && hex2mem((char*)ptr, &get_registers(regbuf)[addr], sizeof(uint32_t))
                        ) {
                    set_registers(regbuf);
                    strcpy(remcomOutBuffer, "OK");
                } else {
                    strcpy(remcomOutBuffer,"E01");
                }
                break;

            case 'm':  /* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
                /* Try to read %x,%x */
                if (hexToInt(&ptr, &addr)
                        && *ptr++ == ','
                        && hexToInt(&ptr, &length))
                {
                    if (length < 0 || (size_t)length > (size_t)GDB_MAX_PACKET_PAYLOAD / 2) {
                        strcpy(remcomOutBuffer,"E01");
                        break;
                    }
                    if (!gdb_ensure_out_buffer((size_t)length * 2 + 1)) {
                        strcpy(remcomOutBuffer,"E01");
                        break;
                    }
                    ramaddr = virt_mem_ptr(addr, length);
                    if (ramaddr) {
                        if (mem2hex(ramaddr, remcomOutBuffer, length))
                            break;
                        strcpy(remcomOutBuffer, "E03");
                        break;
                    }
                    if (length > 0)
                        memset(remcomOutBuffer, '0', (size_t)length * 2);
                    remcomOutBuffer[(size_t)length * 2] = 0;
                } else
                    strcpy(remcomOutBuffer,"E01");
                break;

            case 'M': /* MAA..AA,LLLL: Write LLLL bytes at address AA..AA  */
                /* Try to read '%x,%x:' */
                if (hexToInt(&ptr, &addr)
                        && *ptr++ == ','
                        && hexToInt(&ptr, &length)
                        && *ptr++ == ':')
                {
                    ramaddr = virt_mem_ptr(addr, length);
                    if (!ramaddr) {
                        strcpy(remcomOutBuffer, "E03");
                        break;
                    }
                    if (range_translated((uintptr_t)ramaddr, (uintptr_t)((char *)ramaddr + length)))
                        flush_translations();
                    if (hex2mem(ptr, ramaddr, length))
                        strcpy(remcomOutBuffer, "OK");
                    else
                        strcpy(remcomOutBuffer, "E03");
                } else
                    strcpy(remcomOutBuffer, "E02");
                break;

            case 'S': /* Ssig[;AA..AA] Step with signal at address AA..AA(optional). Same as 's' for us. */
                ptr = strchr(ptr, ';'); /* skip the signal */
                if (ptr)
                    ptr++;
                // fallthrough
            case 's': /* s[AA..AA]  Step at address AA..AA(optional) */
                cpu_events |= EVENT_DEBUG_STEP;
                goto parse_new_pc;
            case 'C': /* Csig[;AA..AA] Continue with signal at address AA..AA(optional). Same as 'c' for us. */
                ptr = strchr(ptr, ';'); /* skip the signal */
                if (ptr)
                    ptr++;
                // fallthrough
            case 'c':    /* c[AA..AA]    Continue at address AA..AA(optional) */
parse_new_pc:
                if (ptr && hexToInt(&ptr, &addr)) {
                    arm.reg[15] = addr;
                }

                gui_debugger_entered_or_left(in_debugger = false);
                return;
            case 'q': /* qString Get value of String */
                if (!strcmp("Offsets", ptr))
                {
                    /* Offsets of sections */
                    sprintf(remcomOutBuffer, "Text=%x;Data=%x;Bss=%x",
                            ndls_debug_alloc_block, ndls_debug_alloc_block,	ndls_debug_alloc_block);
                }
                else if(!strcmp("C", ptr))
                {
                    /* Current thread id */
                    strcpy(remcomOutBuffer, "QC1"); // First and only thread
                }
                else if(!strcmp("fThreadInfo", ptr))
                {
                    /* First thread id */
                    strcpy(remcomOutBuffer, "m1"); // First and only thread
                }
                else if(!strcmp("sThreadInfo", ptr))
                {
                    /* Next thread id */
                    strcpy(remcomOutBuffer, "l"); // No more threads
                }
                else if(!strcmp("HostInfo", ptr))
                {
                    /* Host information */
                    strcpy(remcomOutBuffer, "cputype:12;cpusubtype:7;endian:little;ptrsize:4;");
                }
                else if(!strncmp("Supported", ptr, 9))
                {
                    /* Feature query */
                    if (!gdb_ensure_out_buffer(128)) {
                        strcpy(remcomOutBuffer, "E01");
                        break;
                    }
                    snprintf(remcomOutBuffer, remcomOutCapacity,
                             "PacketSize=%zx;qXfer:features:read+;qXfer:memory-map:read+;"
                             "qMemoryRegionInfo+;qProcessInfo+;qStructuredDataPlugins+;"
                             "qShlibInfoAddr+;vContSupported",
                             (size_t)GDB_MAX_PACKET_PAYLOAD);
                }
                else if(!strcmp("VAttachOrWaitSupported", ptr))
                {
                    strcpy(remcomOutBuffer, "OK");
                }
                else if(!strcmp("ProcessInfo", ptr))
                {
                    strcpy(remcomOutBuffer,
                           "pid:1;parent-pid:0;real-uid:0;real-gid:0;"
                           "effective-uid:0;effective-gid:0;endian:little;"
                           "ptrsize:4;arch:arm;");
                }
                else if(!strcmp("StructuredDataPlugins", ptr))
                {
                    strcpy(remcomOutBuffer, "");
                }
                else if(!strcmp("ShlibInfoAddr", ptr))
                {
                    strcpy(remcomOutBuffer, "0");
                }
                else if(!strncmp("MemoryRegionInfo:", ptr, 17))
                {
                    char *p = ptr + 17;
                    int addr = 0;
                    if (hexToInt(&p, &addr) && gdb_reply_memory_region_info((uint32_t)addr))
                    {
                        // already filled
                    }
                    else
                    {
                        strcpy(remcomOutBuffer, "E01");
                    }
                }
                else if(!strncmp("Xfer:features:read:target.xml", ptr, 29)
                        && (ptr[29] == ':' || ptr[29] == ';'))
                {
                    char *p = ptr + 30;
                    int offset = 0, length = 0;
                    size_t total = strlen(gdb_target_xml);
                    if (hexToInt(&p, &offset) && *p == ',') {
                        ++p;
                        if (hexToInt(&p, &length))
                    {
                        if (offset < 0 || (size_t)offset >= total)
                        {
                            if (!gdb_ensure_out_buffer(2)) {
                                strcpy(remcomOutBuffer, "E01");
                                break;
                            }
                            remcomOutBuffer[0] = 'l';
                            remcomOutBuffer[1] = 0;
                        }
                        else
                        {
                            size_t chunk = total - offset;
                            if (length >= 0 && chunk > (size_t)length)
                                chunk = (size_t)length;
                            if (!gdb_ensure_out_buffer(chunk + 2)) {
                                strcpy(remcomOutBuffer, "E01");
                                break;
                            }
                            remcomOutBuffer[0] = (offset + chunk >= total) ? 'l' : 'm';
                            memcpy(remcomOutBuffer + 1, gdb_target_xml + offset, chunk);
                            remcomOutBuffer[1 + chunk] = 0;
                        }
                    } else {
                        strcpy(remcomOutBuffer, "E01");
                        }
                    } else {
                        strcpy(remcomOutBuffer, "E01");
                    }
                }
                else if(!strncmp("Xfer:memory-map:read:", ptr, 21))
                {
                    char *p = ptr + 21;
                    char *sep = strrchr(p, ':');
                    if (!sep) {
                        strcpy(remcomOutBuffer, "E01");
                        break;
                    }
                    p = sep + 1;
                    int offset = 0, length = 0;
                    size_t total = 0;
                    if (!gdb_build_memory_map(&total)) {
                        strcpy(remcomOutBuffer, "E01");
                        break;
                    }
                    if (hexToInt(&p, &offset) && *p == ',') {
                        ++p;
                        if (hexToInt(&p, &length))
                        {
                            if (offset < 0 || (size_t)offset >= total)
                            {
                                if (!gdb_ensure_out_buffer(2)) {
                                    strcpy(remcomOutBuffer, "E01");
                                    break;
                                }
                                remcomOutBuffer[0] = 'l';
                                remcomOutBuffer[1] = 0;
                            }
                            else
                            {
                                size_t chunk = total - (size_t)offset;
                                if (length >= 0 && chunk > (size_t)length)
                                    chunk = (size_t)length;
                                if (!gdb_ensure_out_buffer(chunk + 2)) {
                                    strcpy(remcomOutBuffer, "E01");
                                    break;
                                }
                                remcomOutBuffer[0] = ((size_t)offset + chunk >= total) ? 'l' : 'm';
                                memcpy(remcomOutBuffer + 1, gdb_memory_map_buf + offset, chunk);
                                remcomOutBuffer[1 + chunk] = 0;
                            }
                        }
                        else
                        {
                            strcpy(remcomOutBuffer, "E01");
                        }
                    }
                    else
                    {
                        strcpy(remcomOutBuffer, "E01");
                    }
                }
                else if(!strncmp("Rcmd,", ptr, 5))
                {
                    gui_debug_printf("GDB Rcmd raw: %s\n", ptr);
                    char *hexcmd = ptr + 5;
                    size_t hexlen = strlen(hexcmd);
                    if (hexlen % 2) {
                        strcpy(remcomOutBuffer, "E01");
                        break;
                    }
                    size_t bytelen = hexlen / 2;
                    char cmd_buf[256];
                    if (bytelen >= sizeof(cmd_buf))
                        bytelen = sizeof(cmd_buf) - 1;
                    hex2mem(hexcmd, cmd_buf, (int)bytelen);
                    cmd_buf[bytelen] = 0;

                    gui_debug_printf("GDB Rcmd decoded: '%s'\n", cmd_buf);
                    char *cmd = cmd_buf;
                    while (*cmd && isspace((unsigned char)*cmd))
                        cmd++;
                    if (!strncmp(cmd, "monitor ", 8))
                        cmd += 8;
                    size_t cmd_len = strlen(cmd);
                    while (cmd_len > 0 && isspace((unsigned char)cmd[cmd_len - 1]))
                        cmd[--cmd_len] = 0;

                    gui_debug_printf("GDB Rcmd normalized: '%s'\n", cmd);
                    if (!strcmp(cmd, "info mem")) {
                        size_t map_len = 0;
                        if (!gdb_build_memory_map(&map_len)) {
                            strcpy(remcomOutBuffer, "E01");
                            break;
                        }
                        gdb_send_console_text(gdb_memory_map_buf);
                        gdb_send_console_text("\n");
                        strcpy(remcomOutBuffer, "OK");
                    } else {
                        char cmd_lower[256];
                        size_t cmd_len = strlen(cmd);
                        if (cmd_len >= sizeof(cmd_lower))
                            cmd_len = sizeof(cmd_lower) - 1;
                        for (size_t i = 0; i < cmd_len; ++i)
                            cmd_lower[i] = (char)tolower((unsigned char)cmd[i]);
                        cmd_lower[cmd_len] = 0;

                        const char *sub = NULL;
                        if (!strncmp(cmd_lower, "fb ", 3))
                            sub = cmd_lower + 3;
                        else if (!strncmp(cmd_lower, "firebird ", 9))
                            sub = cmd_lower + 9;

                        if (sub) {
                            while (*sub && isspace((unsigned char)*sub))
                                ++sub;
                            if (!strncmp(sub, "memmap", 6)) {
                                const char *arg = sub + 6;
                                while (*arg && isspace((unsigned char)*arg))
                                    ++arg;
                                bool use_text = false;
                                if (*arg) {
                                    if (!strncmp(arg, "text", 4) ||
                                        !strncmp(arg, "compact", 7) ||
                                        !strncmp(arg, "fbmap", 5)) {
                                        use_text = true;
                                    }
                                }
                                size_t map_len = 0;
                                if (use_text) {
                                    if (!gdb_build_fb_map(&map_len)) {
                                        strcpy(remcomOutBuffer, "E01");
                                        break;
                                    }
                                    gdb_send_console_text(gdb_fb_map_buf);
                                } else {
                                    if (!gdb_build_memory_map(&map_len)) {
                                        strcpy(remcomOutBuffer, "E01");
                                        break;
                                    }
                                    gdb_send_console_text(gdb_memory_map_buf);
                                }
                                gdb_send_console_text("\n");
                                strcpy(remcomOutBuffer, "OK");
                            } else if (!strncmp(sub, "info", 4)) {
                                const char *model = emulate_cx2 ? "cx2"
                                                    : (emulate_cx ? "cx"
                                                    : (emulate_casplus ? "casplus" : "classic"));
                                char info[256];
                                int written = snprintf(info, sizeof(info),
                                                       "arch=arm\nendian=little\ncpu=arm926ejs\nmodel=%s\n"
                                                       "product=0x%03x\nfeatures=0x%08x\nasic_user_flags=0x%08x\n"
                                                       "sdram=0x%08x\n",
                                                       model, product, features, asic_user_flags,
                                                       mem_areas[1].size);
                                if (written > 0)
                                    gdb_send_console_text(info);
                                strcpy(remcomOutBuffer, "OK");
                            } else {
                                strcpy(remcomOutBuffer, "");
                            }
                        } else {
                            strcpy(remcomOutBuffer, "");
                        }
                    }
                }
                else if(!strcmp("Symbol::", ptr))
                {
                    /* Symbols can be queried */
                    strcpy(remcomOutBuffer, "OK");
                }
                else
                    gui_debug_printf("Unsupported GDB cmd '%s'\n", ptr - 1);

                break;
            case 'v':
                if(!strcmp("Cont?", ptr)) {
                    strcpy(remcomOutBuffer, "");
                } else if (!strncmp("Run", ptr, 3)) {
                    strcpy(remcomOutBuffer, "OK");
                } else if (!strncmp("File:", ptr, 5)) {
                    gdb_hostio_handle_vfile(ptr, NULL);
                } else {
                    gui_debug_printf("Unsupported GDB cmd '%s'\n", ptr - 1);
                }

                break;
            case 'Z': /* 0|1|2|3|4,addr,kind  */
            case 'z': /* 0|1|2|3|4,addr,kind  */
                set = *(ptr - 1) == 'Z';
                // kinds other than 4 aren't supported
                ptr1 = ptr++;
                ptr = strtok(ptr, ",");
                if (ptr && hexToInt(&ptr, &addr) && (ramaddr = virt_mem_ptr(addr & ~3, 4))) {
                    uint32_t *flags = &RAM_FLAGS(ramaddr);
                    switch (*ptr1) {
                        case '0': // mem breakpoint
                        case '1': // hw breakpoint
                            if (set) {
                                if (*flags & RF_CODE_TRANSLATED) flush_translations();
                                *flags |= RF_EXEC_BREAKPOINT;
                            } else
                                *flags &= ~RF_EXEC_BREAKPOINT;
                            break;
                        case '2': // write watchpoint
                        case '4': // access watchpoint
                            if (set) *flags |= RF_WRITE_BREAKPOINT;
                            else *flags &= ~RF_WRITE_BREAKPOINT;
                            if (*ptr1 != '4')
                                break;
                            // fallthrough
                        case '3': // read watchpoint, access watchpoint
                            if (set) *flags |= RF_READ_BREAKPOINT;
                            else *flags &= ~RF_READ_BREAKPOINT;
                            break;
                        default:
                            goto reply;
                    }
                    strcpy(remcomOutBuffer, "OK");
                } else
                    strcpy(remcomOutBuffer, "E01");
                break;
        }			/* switch */

reply:
        /* reply to the request */
        if (reply && !putpacket(remcomOutBuffer))
            goto disconnect;
    }

disconnect:
    gdbstub_disconnect();
    gui_debugger_entered_or_left(in_debugger = false);
}

void gdbstub_reset(void) {
    ndls_debug_alloc_block = 0; // the block is obvisouly freed by the OS on reset
}

static void gdbstub_disconnect(void) {
    gui_status_printf("GDB disconnected.");
#ifdef __MINGW32__
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
    socket_fd = -1;
    gdb_connected = false;
    gdb_local_action = GDB_LOCAL_NONE;
    gdb_waiting_for_attach = false;
    for (size_t i = 0; i < GDB_HOSTIO_MAX_FDS; ++i) {
        if (gdb_hostio_fds[i].used) {
            close(gdb_hostio_fds[i].fd);
            gdb_hostio_fds[i].used = false;
        }
    }
    if (ndls_is_installed())
        armloader_load_snippet(SNIPPET_ndls_debug_free, NULL, 0, NULL);
}

/* Non-blocking poll. Enter the debugger loop if a message is received. */
void gdbstub_recv(void) {
    if(listen_socket_fd == -1)
        return;

    int ret, on;
    if (socket_fd == -1) {
        socket_fd = accept(listen_socket_fd, NULL, NULL);
        if (socket_fd == -1)
            return;
        set_nonblocking(socket_fd, true);
        /* Disable Nagle for low latency */
        on = 1;
#ifdef __MINGW32__
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
#else
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
#endif
        if (ret == -1)
            log_socket_error("setsockopt(TCP_NODELAY) failed for GDB stub socket");

        /* Interface with Ndless */
        if (ndls_is_installed())
        {
            armloader_load_snippet(SNIPPET_ndls_debug_alloc, NULL, 0, gdb_connect_ndls_cb);
            ndls_debug_received = false;
        }
        else
        {
            emuprintf("Ndless not detected or too old. Debugging of applications not available!\n");
            ndls_debug_received = true;
        }

        gdb_hostio_reset_fds();
        gdb_connected = true;
        gdb_handshake_complete = false;
        gui_status_printf("GDB connected.");
    }

    // Wait until we know the program location
    if(!ndls_debug_received)
        return;

    if (gdb_waiting_for_attach)
        return;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET((unsigned)socket_fd, &rfds);
    ret = select(socket_fd + 1, &rfds, NULL, NULL, &(struct timeval) {0, 0});
    if (ret == -1 && errno == EBADF) {
        gdbstub_disconnect();
    }
    else if (ret)
    {
        if(!gdb_handshake_complete)
        {
            gdb_handshake_complete = true;
            gdbstub_loop();
        }
        else
            gdbstub_debugger(DBG_USER, 0);
    }
}

/* addr is only required for read/write breakpoints */
void gdbstub_debugger(enum DBG_REASON reason, uint32_t addr) {
    cpu_events &= ~EVENT_DEBUG_STEP;
    char addrstr[9]; // 8 digits
    snprintf(addrstr, sizeof(addrstr), "%x", addr);
    switch (reason) {
        case DBG_WRITE_BREAKPOINT:
            send_stop_reply(SIGNAL_TRAP, "watch", addrstr);
            break;
        case DBG_READ_BREAKPOINT:
            send_stop_reply(SIGNAL_TRAP, "rwatch", addrstr);
            break;
        default:
            send_stop_reply(SIGNAL_TRAP, NULL, 0);
    }
    gdbstub_loop();
}

void gdbstub_quit()
{
    if(listen_socket_fd != -1)
    {
        #ifdef __MINGW32__
            closesocket(listen_socket_fd);
        #else
            close(listen_socket_fd);
        #endif
        listen_socket_fd = -1;
    }

    if(socket_fd != -1)
    {
        #ifdef __MINGW32__
            closesocket(socket_fd);
        #else
            close(socket_fd);
        #endif
        socket_fd = -1;
    }

    free(remcomInBuffer);
    remcomInBuffer = NULL;
    remcomInCapacity = 0;
    free(remcomOutBuffer);
    remcomOutBuffer = NULL;
    remcomOutCapacity = 0;
    free(gdb_memory_map_buf);
    gdb_memory_map_buf = NULL;
    gdb_memory_map_cap = 0;
    free(gdb_fb_map_buf);
    gdb_fb_map_buf = NULL;
    gdb_fb_map_cap = 0;
}
