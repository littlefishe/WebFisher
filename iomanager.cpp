#include "iomanager.h"
// #include "macro.h"
#include "log.h"
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>

namespace fisher {

static Logger::LoggerRef g_logger = FISHER_LOG_NAME("system");

enum EpollCtlOp {
};

static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl) \
        case ctl: \
            return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default:
            return os << (int)op;
    }
#undef XX
}

static std::ostream& operator<< (std::ostream& os, EPOLL_EVENTS events) {
    if(!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E) \
    if(events & E) { \
        if(!first) { \
            os << "|"; \
        } \
        os << #E; \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch(event) {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            // SYLAR_ASSERT2(false, "getContext");
            std::cout << "failed getcontext" << std::endl;
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx = (std::function<void()>) nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    assert(events & event);
    events = (Event)(events & ~event);
    EventContext& ctx = getContext(event);
    std::visit([](auto k){
        IOManager::GetThis()->schedule(k);
    }, ctx);
    return;
}

IOManager::IOManager(size_t threads, const std::string& name)
    :Scheduler(threads, name) {
    epfd_ = epoll_create(5000);
    assert(epfd_ > 0);

    int rt = pipe(tickleFds_);
    assert(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = tickleFds_[0];

    rt = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
    assert(!rt);

    rt = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
    assert(!rt);

    contextResize(32);
    start();
}

IOManager::~IOManager() {
    // stop();
    close(epfd_);
    close(tickleFds_[0]);
    close(tickleFds_[1]);
    for(size_t i = 0; i < fdContexts_.size(); ++i) {
        if(fdContexts_[i]) {
            delete fdContexts_[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    fdContexts_.resize(size);

    for(size_t i = 0; i < fdContexts_.size(); ++i) {
        if(!fdContexts_[i]) {
            fdContexts_[i] = new FdContext;
            fdContexts_[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx;
    {
        std::unique_lock lock(mutex_);
        if((int)fdContexts_.size() > fd) {
            fd_ctx = fdContexts_[fd];
        } else {
            contextResize(fd * 1.5);
            fd_ctx = fdContexts_[fd];
        }
    }

    std::unique_lock lock2(fd_ctx->mutex);
    if(fd_ctx->events & event) {
        FISHER_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                    << " event=" << (EPOLL_EVENTS)event
                    << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        assert(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    fd_ctx->events = (Event)(fd_ctx->events | event);
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    // originally, no ctx
    assert(!event_ctx.index());
    
    // assign ctx
    if(cb) {
        event_ctx = cb;
    } else {
        event_ctx = Fiber::GetThis();
        // SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC
        //               ,"state=" << event_ctx.fiber->getState());
        assert(std::get<Fiber::FiberRef>(event_ctx)->getState() == Fiber::EXEC);
    }

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if(rt) {
        FISHER_LOG_ERROR(g_logger) << "epoll_ctl(" << epfd_ << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
            << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }
    ++n_pendingEvent_;
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    FdContext* fd_ctx;
    {
        std::shared_lock lock(mutex_);
        if((int)fdContexts_.size() <= fd) {
            return false;
        }
        fd_ctx = fdContexts_[fd];
    }

    std::unique_lock lock(fd_ctx->mutex);
    if(!(fd_ctx->events & event)) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if(rt) {
        FISHER_LOG_ERROR(g_logger) << "epoll_ctl(" << epfd_ << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --n_pendingEvent_;
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    FdContext* fd_ctx;
    {
        std::shared_lock lock(mutex_);
        if((int)fdContexts_.size() <= fd) {
            return false;
        }
        fd_ctx = fdContexts_[fd];
    }

    std::unique_lock lock(fd_ctx->mutex);
    if(!(fd_ctx->events & event)) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if(rt) {
        FISHER_LOG_ERROR(g_logger) << "epoll_ctl(" << epfd_ << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    fd_ctx->triggerEvent(event);
    --n_pendingEvent_;
    return true;
}

bool IOManager::cancelAll(int fd) {
    FdContext* fd_ctx;
    {
        std::shared_lock lock(mutex_);
        if((int)fdContexts_.size() <= fd) {
            return false;
        }
        fd_ctx = fdContexts_[fd];
    }

    std::unique_lock lock(fd_ctx->mutex);
    if(!fd_ctx->events) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(epfd_, op, fd, &epevent);
    if(rt) {
        FISHER_LOG_ERROR(g_logger) << "epoll_ctl(" << epfd_ << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    if(fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --n_pendingEvent_;
    }
    if(fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --n_pendingEvent_;
    }

    assert(fd_ctx->events == 0);
    return true;
}

IOManager* IOManager::GetThis() {
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

void IOManager::tickle() {
    if(!hasIdleThreads()) {
        return;
    }
    int rt = write(tickleFds_[1], "T", 1);
    assert(rt == 1);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    return timeout == ~0ull && n_pendingEvent_ == 0 && Scheduler::stopping();
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

void IOManager::idle() {
    const uint64_t MAX_EVENTS = 256;
    epoll_event* events = new epoll_event[MAX_EVENTS]();
    // std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
    //     delete[] ptr;
    // });
    static const uint64_t MAX_TIMEOUT = 10000;
    while(true) {
        uint64_t next_timeout = ~0ull;
        if(stopping(next_timeout)) {
            FISHER_LOG_INFO(g_logger) << "tickle " << n_idle_thread_;
            break;
        }
        FISHER_LOG_INFO(g_logger) << "idle";
        if(next_timeout != ~0ull) {
            next_timeout = std::min(next_timeout, MAX_TIMEOUT);
        } else {
            next_timeout = MAX_TIMEOUT;
        }
        int rt = epoll_wait(epfd_, events, MAX_EVENTS, (int)next_timeout);
        if (rt <= 0 || errno == EINTR) {
            continue;
        }
        FISHER_LOG_INFO(g_logger) << "wake up";
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        for (auto it = cbs.begin(); it != cbs.end(); it++) {
            schedule(*it);
        }

        for(int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];
            if(event.data.fd == tickleFds_[0]) {
                uint8_t dummy[256];
                while(read(tickleFds_[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            std::unique_lock lock(fd_ctx->mutex);
            if(event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if(event.events & EPOLLIN) {
                real_events |= READ;
            }
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            if(int rt2 = epoll_ctl(epfd_, op, fd_ctx->fd, &event)) {
                FISHER_LOG_ERROR(g_logger) << "epoll_ctl(" << epfd_ << ", "
                    << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            if(real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --n_pendingEvent_;
            }
            if(real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --n_pendingEvent_;
            }
        }
        Fiber::GetThis()->yeild();
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}