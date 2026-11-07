/* Offline LibreOffice compatibility, without enabling kernel sockets.
 * Its process-launch handshake is one-way (parent reads, child writes).
 * The single-instance listener is unnecessary with a fresh profile per run;
 * model that listener with an anonymous pipe and refuse all clients.
 * Loaded only by the local soffice wrapper, never the sandbox supervisor.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#define FD_CAP 4096
static unsigned char listeners[FD_CAP];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int state(int fd) {
    int value = 0;
    pthread_mutex_lock(&lock);
    if (fd >= 0 && fd < FD_CAP) value = listeners[fd];
    pthread_mutex_unlock(&lock);
    return value;
}

int socket(int domain, int type, int protocol) {
    if (domain != AF_UNIX || (type & 0xf) != SOCK_STREAM || protocol != 0)
        return syscall(SYS_socket, domain, type, protocol);
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) return -1;
    syscall(SYS_close, p[1]);
    if (p[0] >= FD_CAP) {
        syscall(SYS_close, p[0]); errno = EMFILE; return -1;
    }
    pthread_mutex_lock(&lock); listeners[p[0]] = 1; pthread_mutex_unlock(&lock);
    return p[0];
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    if (domain == AF_UNIX && (type & 0xf) == SOCK_STREAM && protocol == 0)
        return pipe2(sv, (type & SOCK_CLOEXEC) ? O_CLOEXEC : 0);
    return syscall(SYS_socketpair, domain, type, protocol, sv);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    if (!state(fd)) return syscall(SYS_bind, fd, addr, len);
    const struct sockaddr_un *un = (const struct sockaddr_un *)addr;
    const char prefix[] = "/tmp/OSL_PIPE_";
    if (len < offsetof(struct sockaddr_un, sun_path) + sizeof(prefix) ||
        un->sun_family != AF_UNIX || memcmp(un->sun_path, prefix, sizeof(prefix)-1)) {
        errno = EPERM; return -1;
    }
    pthread_mutex_lock(&lock); listeners[fd] = 2; pthread_mutex_unlock(&lock);
    return 0;
}

int listen(int fd, int backlog) {
    if (state(fd) == 2) return 0;
    return syscall(SYS_listen, fd, backlog);
}

int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    if (state(fd)) { errno = ECONNABORTED; return -1; }
    return syscall(SYS_accept, fd, addr, len);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    if (state(fd)) { errno = ECONNREFUSED; return -1; }
    return syscall(SYS_connect, fd, addr, len);
}

int shutdown(int fd, int how) {
    if (state(fd)) return 0;
    return syscall(SYS_shutdown, fd, how);
}

int close(int fd) {
    pthread_mutex_lock(&lock);
    if (fd >= 0 && fd < FD_CAP) listeners[fd] = 0;
    int result = syscall(SYS_close, fd);
    pthread_mutex_unlock(&lock);
    return result;
}
