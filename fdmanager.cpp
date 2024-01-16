#include "fdmanager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>

namespace fisher {

FdCtx::FdCtx(int fd)
    :isInit_(false)
    ,isSocket_(false)
    ,nonblock_(false)
    ,isClosed_(false)
    ,fd_(fd)
    ,recvTimeout_(-1)
    ,sendTimeout_(-1) {
    init();
}

FdCtx::~FdCtx() {
}

bool FdCtx::init() {
    if(isInit_) {
        return true;
    }
    recvTimeout_ = -1;
    sendTimeout_ = -1;

    struct stat fd_stat;
    // no such fd
    if(-1 == fstat(fd_, &fd_stat)) {
        isInit_ = false;
        isSocket_ = false;
    } else {
        isInit_ = true;
        isSocket_ = S_ISSOCK(fd_stat.st_mode);
    }

    if(isSocket_) {
        int flags = fcntl(fd_, F_GETFL, 0);
        if(!(flags & O_NONBLOCK)) {
            fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        }
        nonblock_ = true;
    } else {
        nonblock_ = false;
    }

    isClosed_ = false;
    return isInit_;
}

void FdCtx::setTimeout(int type, uint64_t v) {
    if(type == SO_RCVTIMEO) {
        recvTimeout_ = v;
    } else {
        sendTimeout_ = v;
    }
}

uint64_t FdCtx::getTimeout(int type) {
    if(type == SO_RCVTIMEO) {
        return recvTimeout_;
    } else {
        return sendTimeout_;
    }
}

FdManager::FdManager() {
    datas_.resize(64);
}

FdCtx::FdCtxRef FdManager::get(int fd, bool auto_create) {
    if(fd == -1) {
        return nullptr;
    }
    {
        std::shared_lock lock(mutex_);
        if((int)datas_.size() > fd && datas_[fd]) {
            return datas_[fd];
        }
    }
    if(auto_create == false) {
        return nullptr;
    }
    std::unique_lock lock(mutex_);
    if(fd >= (int)datas_.size()) {
        datas_.resize(fd * 1.5);
    }
    datas_[fd] = std::make_shared<FdCtx>(fd);
    return datas_[fd];
}

void FdManager::del(int fd) {
    std::unique_lock lock(mutex_);
    if((int)datas_.size() <= fd) {
        return;
    }
    datas_[fd].reset();
}

}