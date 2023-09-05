#include "timer.h"
#include "util.h"
#include <mutex>
#include <algorithm>

namespace fisher {

bool Timer::Comparator::operator()(const Timer::TimerRef& lhs, const Timer::TimerRef& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    return lhs->next_ < rhs->next_;
}


Timer::Timer(uint64_t ms, std::function<void()> cb,
             bool recurring, TimerManager* manager)
    :recurring_(recurring)
    ,ms_(ms)
    ,cb_(cb)
    ,mgr_(manager) {
    next_ = GetCurrentMS() + ms_;
}

Timer::Timer(uint64_t next)
    :next_(next) {
}

bool Timer::cancel() {
    std::unique_lock lock(mgr_->mutex_);
    if(cb_) {
        cb_ = nullptr;
        auto it = mgr_->timers_.find(shared_from_this());
        mgr_->timers_.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() {
    std::unique_lock lock(mgr_->mutex_);
    if(!cb_) {
        return false;
    }
    auto it = mgr_->timers_.find(shared_from_this());
    if(it == mgr_->timers_.end()) {
        return false;
    }
    mgr_->timers_.erase(it);
    next_ = GetCurrentMS() + ms_;
    mgr_->timers_.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    if (ms == ms_ && !from_now) {
        return false;
    }
    std::unique_lock lock(mgr_->mutex_);
    if(!cb_) {
        return false;
    }
    auto it = mgr_->timers_.find(shared_from_this());
    if(it == mgr_->timers_.end()) {
        return false;
    }
    mgr_->timers_.erase(it);
    ms_ = ms;
    if (from_now) {
        next_ = GetCurrentMS() + ms_;
    }
    mgr_->addTimer(shared_from_this());
    return true;
}

void TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
    Timer::TimerRef timer(new Timer(ms, cb, recurring, this));
    std::unique_lock lock(mutex_);
    addTimer(timer);
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    if(auto observe = weak_cond.lock()) {
        cb();
    }
}

Timer::TimerRef TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
                                    ,std::weak_ptr<void> weak_cond
                                    ,bool recurring) {
    addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer() {
    std::shared_lock lock(mutex_);
    tickled_ = false;
    if(timers_.empty()) {
        return ~0ull;
    }

    const Timer::TimerRef next_timer = *timers_.begin();
    uint64_t now_ms = GetCurrentMS();
    return std::max(next_timer->next_ - now_ms, 0ul);
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    std::vector<Timer::TimerRef> expired;
    {
        std::shared_lock lock(mutex_);
        if(timers_.empty()) {
            return;
        }
    }
    std::unique_lock lock(mutex_);
    if(timers_.empty()) {
        return;
    }

    uint64_t now_ms = GetCurrentMS();
    Timer::TimerRef now_timer(new Timer(now_ms));

    // get all timer from begin to iter with next_ <= now
    auto it = timers_.lower_bound(now_timer);
    while(it != timers_.end() && (*it)->next_ <= now_ms) {
        ++it;
    }
    expired.insert(expired.begin(), timers_.begin(), it);
    timers_.erase(timers_.begin(), it);
    cbs.reserve(expired.size());

    for(auto& timer : expired) {
        cbs.push_back(timer->cb_);
        if(timer->recurring_) {
            timer->next_ = now_ms + timer->ms_;
            timers_.insert(timer);
        }
    }
}

void TimerManager::addTimer(Timer::TimerRef timer) {
    auto it = timers_.insert(timer).first;
    bool at_front = (it == timers_.begin()) && !tickled_;
    if(at_front) {
        tickled_ = true;
        onTimerInsertedAtFront();
    }
}

bool TimerManager::hasTimer() {
    std::shared_lock lock(mutex_);
    return !timers_.empty();
}

}