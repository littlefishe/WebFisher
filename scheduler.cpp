#include <unistd.h>
#include "scheduler.h"
#include "log.h"
// #include "macro.h"
// #include "hook.h"

namespace fisher {

static fisher::Logger::LoggerRef g_logger = FISHER_LOG_NAME("system");

static thread_local Scheduler* t_scheduler = nullptr;
static thread_local Fiber::FiberRef t_mainfiber = nullptr;

static thread_local uint64_t tid = 0;
static thread_local std::string t_name = "thr m";

uint64_t Scheduler::GetThreadId() {
    return tid;
}

std::string Scheduler::GetThreadName() {
    return t_name;
}


Scheduler::Scheduler(size_t threads, const std::string& name)
    :name_(name), n_thread_(threads) {
    // SYLAR_ASSERT(threads > 0);
    threadpool_.resize(n_thread_);
}

Scheduler::~Scheduler() {
    // SYLAR_ASSERT(m_stopping);
    if(GetThis() == this) {
        t_scheduler = nullptr;
    }
}

Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}


void Scheduler::start() {
    std::unique_lock ul(latch_);
    if(try_stop_) {
        return;
    }
    // SYLAR_ASSERT(m_threads.empty());
    for(size_t i = 0; i < n_thread_; ++i) {
        threadpool_[i] = std::thread(
            [=] {
                tid = i + 1;
                t_name = "thr " + std::to_string(i + 1);
                run();
            });
    }
}

void Scheduler::stop() {
    auto_stop_ = true;
    // SYLAR_ASSERT(GetThis() != this);
    try_stop_ = true;
    for(size_t i = 0; i < n_thread_; ++i) {
        tickle();
    }
    for(auto & i : threadpool_) {
        i.join();
    }
}

void Scheduler::setThis() {
    t_scheduler = this;
}

Fiber::FiberRef Scheduler::GetMainFiber() {
    return t_mainfiber;
}

void Scheduler::run() {
    FISHER_LOG_INFO(g_logger) << name_ << " run";
    // set_hook_enable(true);
    setThis();
    t_mainfiber.reset(new Fiber());
    Fiber::FiberRef idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    while(true) {
        bool is_active = false;
        Fiber::FiberRef fbr;
        {
            // get a fiber from list
            std::unique_lock ul(latch_);
            for (auto it = fiber_list_.begin(); it != fiber_list_.end(); it++) {
                if ((*it)->getState() != Fiber::EXEC) {
                    fbr = std::move(*it);
                    fiber_list_.erase(it++);
                    is_active = true;
                    n_active_thread_++;
                    break;
                }
            }
        }
        
        if (is_active) {
            if (fbr->getState() != Fiber::TERM && fbr->getState() != Fiber::EXCEPT) {
                fbr->call();
                // return
                --n_active_thread_;
                if (fbr->getState() == Fiber::READY) {
                    schedule(std::move(fbr));
                } else if (fbr->getState() != Fiber::TERM && fbr->getState() != Fiber::EXCEPT) {
                    fbr->state_ = Fiber::HOLD;
                }
            // fbr has termed or ex
            } else {
                --n_active_thread_;
            }

        } 
        if(idle_fiber->getState() == Fiber::TERM) {
            FISHER_LOG_INFO(g_logger) << "idle fiber term";
            break;
        }

        ++n_idle_thread_;
        idle_fiber->call();
        --n_idle_thread_;
        if(idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
            idle_fiber->state_ = Fiber::HOLD;
        }
    }
}

void Scheduler::tickle() {
    FISHER_LOG_INFO(g_logger) << "tickle";
}

bool Scheduler::stopping() {
    std::unique_lock ul(latch_);
    return try_stop_ && fiber_list_.empty() && !n_active_thread_;
}

void Scheduler::idle() {
    while(!stopping()) {
        FISHER_LOG_INFO(g_logger) << "...";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Fiber::GetThis()->yeild();
    }
}
    
std::ostream& Scheduler::dump(std::ostream& os) {
    os << "[Scheduler name=" << name_
       << " size=" << n_thread_
       << " active_count=" <<n_active_thread_
       << " idle_count=" << n_idle_thread_
       << " stopping=" << try_stop_
       << " ]" << std::endl << "    ";
    for(size_t i = 0; i < threadpool_.size(); ++i) {
        if(i) {
            os << ", ";
        }
        os << threadpool_[i].get_id();
    }
    return os;
}

}