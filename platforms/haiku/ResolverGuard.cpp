// Keeps thread cancellation out of the resolver.
//
// Haiku's nsdispatch() (libnetwork, NetBSD lineage) tracks the threads that are
// inside it with a linked list of nodes that live on each caller's stack. A
// thread cancelled while it is resolving never unlinks its node, and the next
// lookup from any thread walks into the dead stack and crashes with a segment
// violation in nsdispatch(). libVLC does exactly that: when a stream is stopped
// or switched while it is still connecting, it cancels the helper thread it
// runs getaddrinfo() on.
//
// Haiku's runtime loader resolves symbols starting from the executable, so the
// functions below take the place of libnetwork's for every image in the
// process, VLC plugins included. Each one forwards to the real function with
// cancellation switched off and lets a cancel that arrived meanwhile act right
// after, once the list is intact again.
#include <dlfcn.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>

namespace {

template <typename Fn>
Fn realSymbol(const char* name) {
    return reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name));
}

class NoCancel {
public:
    NoCancel() { pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &saved_); }
    ~NoCancel() {
        pthread_setcancelstate(saved_, nullptr);
        pthread_testcancel();
    }

private:
    int saved_;
};

}  // namespace

extern "C" {

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                struct addrinfo** result) {
    using Fn = int (*)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
    static Fn real = realSymbol<Fn>("getaddrinfo");
    NoCancel guard;
    return real(node, service, hints, result);
}

int getnameinfo(const struct sockaddr* address, socklen_t addressLength, char* host,
                socklen_t hostLength, char* service, socklen_t serviceLength, int flags) {
    using Fn = int (*)(const struct sockaddr*, socklen_t, char*, socklen_t, char*, socklen_t, int);
    static Fn real = realSymbol<Fn>("getnameinfo");
    NoCancel guard;
    return real(address, addressLength, host, hostLength, service, serviceLength, flags);
}

struct hostent* gethostbyname(const char* name) {
    using Fn = struct hostent* (*)(const char*);
    static Fn real = realSymbol<Fn>("gethostbyname");
    NoCancel guard;
    return real(name);
}

struct hostent* gethostbyname2(const char* name, int family) {
    using Fn = struct hostent* (*)(const char*, int);
    static Fn real = realSymbol<Fn>("gethostbyname2");
    NoCancel guard;
    return real(name, family);
}

struct hostent* gethostbyaddr(const void* address, socklen_t length, int family) {
    using Fn = struct hostent* (*)(const void*, socklen_t, int);
    static Fn real = realSymbol<Fn>("gethostbyaddr");
    NoCancel guard;
    return real(address, length, family);
}

}  // extern "C"
