#include "hook.h"
#include <dlfcn.h>

#include "log.h"
#include "fiber.h"
#include "iomanager.h"
#include "fdmanager.h"
// #include "macro.h"

static fisher::Logger::LoggerRef g_logger = FISHER_LOG_NAME("system");
namespace fisher {

// static fisher::ConfigVar<int>::ptr g_tcp_connect_timeout =
//     fisher::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");
static uint64_t s_connect_timeout = 5000;

static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(getsockopt) \
    XX(setsockopt)

void hook_init() {
    static bool is_inited = false;
    if(is_inited) {
        return;
    }
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
}

// static uint64_t s_connect_timeout = -1;
struct _HookIniter {
    _HookIniter() {
        hook_init();
        // s_connect_timeout = g_tcp_connect_timeout->getValue();

        // g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
        //         SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from "
        //                                  << old_value << " to " << new_value;
        //         s_connect_timeout = new_value;
        // });
    }
};

static _HookIniter s_hook_initer;

bool is_hook_enable() {
    return t_hook_enable;
}

void set_hook_enable(bool flag) {
    t_hook_enable = flag;
}

}

struct timer_info {
    int cancelled = 0;
};

template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args) {
    if(!fisher::t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);
    }
    fisher::FdCtx::FdCtxRef ctx = fisher::FdMgr::getInstance().get(fd);
    if(!ctx) {
        return fun(fd, std::forward<Args>(args)...);
    }

    if(ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket() || ctx->getNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }

    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

    ssize_t n = fun(fd, std::forward<Args>(args)...);
    while (n == -1) {
        if (errno == EINTR) {
            n = fun(fd, std::forward<Args>(args)...);
            continue;
        }
        if (n == -1 && errno == EAGAIN) {
            fisher::IOManager* iom = fisher::IOManager::GetThis();
            fisher::Timer::TimerRef timer;
            std::weak_ptr<timer_info> winfo(tinfo);

            if(to != (uint64_t)-1) {
                timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                    auto t = winfo.lock();
                    if(!t || t->cancelled) {
                        return;
                    }
                    t->cancelled = ETIMEDOUT;
                    iom->cancelEvent(fd, (fisher::IOManager::Event)(event));
                }, winfo);
            }

            int rt = iom->addEvent(fd, (fisher::IOManager::Event)(event));
            if(rt) {
                // error rt
                FISHER_LOG_ERROR(g_logger) << hook_fun_name << " addEvent("
                    << fd << ", " << event << ")";
                if(timer) {
                    timer->cancel();
                }
                return -1;

            } else {
                fisher::Fiber::GetThis()->yeild();
                if(timer) {
                    timer->cancel();
                }
                if(tinfo->cancelled) {
                    errno = tinfo->cancelled;
                    return -1;
                }
                // try again
                n = fun(fd, std::forward<Args>(args)...);
            }
        // n >= 0
        } else {
            break;
        }
    }
    return n;
}


extern "C" {
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

unsigned int sleep(unsigned int seconds) {
    if(!fisher::t_hook_enable) {
        return sleep_f(seconds);
    }

    fisher::Fiber::FiberRef fiber = fisher::Fiber::GetThis();
    fisher::IOManager* iom = fisher::IOManager::GetThis();
    iom->addTimer(seconds * 1000, std::bind((void(fisher::Scheduler::*)
            (fisher::Fiber::FiberRef, int thread))&fisher::IOManager::schedule
            ,iom, fiber, -1));
    fiber->yeild();
    return 0;
}

int socket(int domain, int type, int protocol) {
    if(!fisher::t_hook_enable) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if(fd == -1) {
        return fd;
    }
    fisher::FdMgr::getInstance().get(fd, true);
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if(!fisher::t_hook_enable) {
        return connect_f(fd, addr, addrlen);
    }
    fisher::FdCtx::FdCtxRef ctx = fisher::FdMgr::getInstance().get(fd);
    if(!ctx || ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket()) {
        return connect_f(fd, addr, addrlen);
    }

    int n = connect_f(fd, addr, addrlen);
    if(n == 0) {
        return 0;
    } else if(n != -1 || errno != EINPROGRESS) {
        return n;
    }

    fisher::IOManager* iom = fisher::IOManager::GetThis();
    fisher::Timer::TimerRef timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if(timeout_ms != (uint64_t)-1) {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
                auto t = winfo.lock();
                // when back from main, the func exits, winfo is released, return directly
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                iom->cancelEvent(fd, fisher::IOManager::WRITE);
        }, winfo);
    }

    int rt = iom->addEvent(fd, fisher::IOManager::WRITE);
    if(rt == 0) {
        fisher::Fiber::GetThis()->yeild();
        if(timer) {
            timer->cancel();
        }
        if(tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    } else {
        if(timer) {
            timer->cancel();
        }
        FISHER_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
        return -1;
    }
    if(!error) {
        return 0;
    } else {
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_with_timeout(sockfd, addr, addrlen, fisher::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = do_io(s, accept_f, "accept", fisher::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    if(fd >= 0) {
        fisher::FdMgr::getInstance().get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", fisher::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", fisher::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", fisher::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", fisher::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", fisher::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", fisher::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", fisher::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", fisher::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", fisher::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", fisher::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) {
    if(!fisher::t_hook_enable) {
        return close_f(fd);
    }

    fisher::FdCtx::FdCtxRef ctx = fisher::FdMgr::getInstance().get(fd);
    if(ctx) {
        auto iom = fisher::IOManager::GetThis();
        if(iom) {
            iom->cancelAll(fd);
        }
        fisher::FdMgr::getInstance().del(fd);
    }
    return close_f(fd);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if(!fisher::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET) {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            fisher::FdCtx::FdCtxRef ctx = fisher::FdMgr::getInstance().get(sockfd);
            if(ctx) {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}