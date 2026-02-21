#include <errno.h>
#include <string.h>

#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#endif

#include "debug.h"
#include "emu.h"

#define MAX_CMD_LEN 300

static int listen_socket_fd = -1;
static int socket_fd = -1;

static void log_socket_error(const char *msg) {
#ifdef __MINGW32__
    int errCode = WSAGetLastError();
    LPSTR errString = NULL;  // will be allocated and filled by FormatMessage
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, errCode, 0, (LPSTR)&errString, 0, 0);
    gui_debug_printf("%s: %s (%i)\n", msg, errString, errCode);
    LocalFree(errString);
#else
    gui_perror(msg);
#endif
}

static void set_nonblocking(int socket, bool nonblocking) {
#ifdef __MINGW32__
    u_long mode = nonblocking;
    ioctlsocket(socket, FIONBIO, &mode);
#else
    int ret = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, nonblocking ? ret | O_NONBLOCK : ret & ~O_NONBLOCK);
    ret = fcntl(socket, F_GETFD, 0);
    fcntl(socket, F_SETFD, ret | FD_CLOEXEC);
#endif
}

bool rdebug_bind(unsigned int port) {
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
        log_socket_error("Remote debug: Failed to create socket");
        return false;
    }
    set_nonblocking(listen_socket_fd, true);

    memset(&sockaddr, '\000', sizeof sockaddr);
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    r = bind(listen_socket_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (r == -1) {
        log_socket_error("Remote debug: failed to bind socket. Check that Firebird is not already running!");
        return false;
    }
    r = listen(listen_socket_fd, 0);
    if (r == -1) {
        log_socket_error("Remote debug: failed to listen on socket");
        return false;
    }

    return true;
}

static char rdebug_inbuf[MAX_CMD_LEN];
static size_t rdebug_inbuf_used = 0;

void rdebug_recv(void) {
    if (listen_socket_fd == -1)
        return;

    int ret, on;
    if (socket_fd == -1) {
        ret = accept(listen_socket_fd, NULL, NULL);
        if (ret == -1)
            return;
        socket_fd = ret;
        set_nonblocking(socket_fd, true);
        /* Disable Nagle for low latency */
        on = 1;
#ifdef __MINGW32__
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
#else
        ret = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
#endif
        if (ret == -1)
            log_socket_error("Remote debug: setsockopt(TCP_NODELAY) failed for socket");
        gui_debug_printf("Remote debug: connected.\n");
        return;
    }

    while (true)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET((unsigned)socket_fd, &rfds);
        struct timeval zero = {0, 100000};
        ret = select(socket_fd + 1, &rfds, NULL, NULL, &zero);
        if (ret == -1 && errno == EBADF) {
            gui_debug_printf("Remote debug: connection closed.\n");
#ifdef __MINGW32__
            closesocket(socket_fd);
#else
            close(socket_fd);
#endif
            socket_fd = -1;
        }
        else if (!ret) // No data available
        {
            if (exiting)
            {
#ifdef __MINGW32__
                closesocket(socket_fd);
#else
                close(socket_fd);
#endif
                socket_fd = -1;
                return;
            }

            gui_do_stuff(false);
        }
        else // Data available
            break;
    }

    size_t buf_remain = sizeof(rdebug_inbuf) - rdebug_inbuf_used;
    if (!buf_remain) {
        gui_debug_printf("Remote debug: command is too long\n");
        return;
    }

#ifdef __MINGW32__
    ssize_t rv = recv(socket_fd, &rdebug_inbuf[rdebug_inbuf_used], buf_remain, 0);
#else
    ssize_t rv = recv(socket_fd, (void *) &rdebug_inbuf[rdebug_inbuf_used], buf_remain, 0);
#endif
    if (!rv) {
        gui_debug_printf("Remote debug: connection closed.\n");
#ifdef __MINGW32__
        closesocket(socket_fd);
#else
        close(socket_fd);
#endif
        socket_fd = -1;
        return;
    }
    if (rv < 0 && errno == EAGAIN) {
        /* no data for now, call back when the socket is readable */
        return;
    }
    if (rv < 0) {
        log_socket_error("Remote debug: connection error");
        return;
    }
    rdebug_inbuf_used += rv;

    char *line_start = rdebug_inbuf;
    char *line_end;
    while ((line_end = (char *)memchr((void *)line_start, '\n', rdebug_inbuf_used - (line_start - rdebug_inbuf)))) {
        *line_end = 0;
        process_debug_cmd(line_start);
        line_start = line_end + 1;
    }
    /* Shift buffer down so the unprocessed data is at the start */
    rdebug_inbuf_used -= (line_start - rdebug_inbuf);
    memmove(rdebug_inbuf, line_start, rdebug_inbuf_used);
}

void rdebug_quit()
{
    if (socket_fd != -1)
    {
#ifdef __MINGW32__
        closesocket(socket_fd);
#else
        close(socket_fd);
#endif
        socket_fd = -1;
    }

    if (listen_socket_fd != -1)
    {
#ifdef __MINGW32__
        closesocket(listen_socket_fd);
#else
        close(listen_socket_fd);
#endif
        listen_socket_fd = -1;
    }
}
