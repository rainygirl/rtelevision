// Makes SOCK_NONBLOCK mean what it says.
//
// On Haiku (seen on R1/beta6 x86_gcc2, hrev99002) a socket created with
// socket(..., type | SOCK_NONBLOCK, ...) reports O_NONBLOCK from fcntl() but
// its recv() still blocks; only a flag set afterwards with fcntl() takes
// effect. libVLC creates every network socket that way and then reads it in a
// loop of non-blocking reads and interruptible polls - the way it aborts a
// transfer when a stream is stopped. With a socket that blocks instead, the
// read never returns, the demuxer's threads never end, and
// libvlc_media_player_stop() waits for them forever: the window froze on Stop
// and the app could not quit.
//
// As with ResolverGuard.cpp, the executable's own definition of socket() is
// the one every image in the process binds to. It creates the socket without
// the flag and sets non-blocking mode with fcntl() afterwards.
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>

namespace {

int applyNonBlocking(int fd, bool wanted) {
    if (fd >= 0 && wanted) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

}  // namespace

extern "C" int socket(int family, int type, int protocol) {
    using Fn = int (*)(int, int, int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "socket"));
    const int fd = real(family, type & ~SOCK_NONBLOCK, protocol);
    return applyNonBlocking(fd, (type & SOCK_NONBLOCK) != 0);
}

extern "C" int accept4(int listener, struct sockaddr* address, socklen_t* length, int flags) {
    using Fn = int (*)(int, struct sockaddr*, socklen_t*, int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "accept4"));
    const int fd = real(listener, address, length, flags & ~SOCK_NONBLOCK);
    return applyNonBlocking(fd, (flags & SOCK_NONBLOCK) != 0);
}
