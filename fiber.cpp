#include <atomic>
#include <cassert>
#include "fiber.h"
// #include "config.h"
// #include "macro.h"
#include "log.h"
#include "scheduler.h"

namespace fisher {

static Logger::LoggerRef g_logger = FISHER_LOG_NAME("system");

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

static thread_local Fiber::FiberRef t_fiber = nullptr; // this fiber

uint32_t g_fiber_stack_size = 8 * 1024;
// static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
//     Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size) {
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

Fiber::Fiber() {
    state_ = State::EXEC;
    if(getcontext(&ctx_)) {
        // SYLAR_ASSERT2(false, "getcontext");
        assert(false);
    }

    ++s_fiber_count;
    FISHER_LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize)
    :fid_(++s_fiber_id), cb_(cb) {
    ++s_fiber_count;
    stacksize_ = stacksize ? stacksize : g_fiber_stack_size;
    stack_ = StackAllocator::Alloc(stacksize);
    if(getcontext(&ctx_)) {
        // SYLAR_ASSERT2(false, "getcontext");
        assert(false);
    }
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = stack_;
    ctx_.uc_stack.ss_size = stacksize_;
    makecontext(&ctx_, &Fiber::MainFunc, 0);
    FISHER_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << fid_;
}

Fiber::~Fiber() {
    --s_fiber_count;
    if(stack_) {
        assert(state_ == TERM || state_ == EXCEPT || state_ == INIT);
        StackAllocator::Dealloc(stack_, stacksize_);
    } else {
        assert(!cb_);
        assert(state_ == EXEC);
    }
}

//重置协程函数，并重置状态
//INIT，TERM, EXCEPT
void Fiber::reset(std::function<void()> cb) {
    assert(stack_);
    assert(state_ == TERM || state_ == EXCEPT || state_ == INIT);
    cb_ = cb;
    if(getcontext(&ctx_)) {
        assert(false);
        // SYLAR_ASSERT2(false, "getcontext");
    }

    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = stack_;
    ctx_.uc_stack.ss_size = stacksize_;

    makecontext(&ctx_, &Fiber::MainFunc, 0);
    state_ = INIT;
}


//切换到当前协程执行
void Fiber::call() {
    SetThis(this->shared_from_this());
    state_ = EXEC;
    if(swapcontext(&Scheduler::GetMainFiber()->ctx_, &ctx_)) {
        // SYLAR_ASSERT2(false, "swapcontext");
        std::cout << "call fiber failed" << std::endl;
    }
}

//切换到后台执行
void Fiber::yeild() {
    SetThis(Scheduler::GetMainFiber());
    if(swapcontext(&ctx_, &Scheduler::GetMainFiber()->ctx_)) {
        // SYLAR_ASSERT2(false, "swapcontext");
        std::cout << "yeild failed" << std::endl;
    }
}

void Fiber::SetThis(FiberRef f) { 
    t_fiber = f; 
}

Fiber::FiberRef Fiber::GetThis() { 
   return t_fiber;
}

//总协程数
uint64_t Fiber::TotalFibers() {
    return s_fiber_count;
}

void Fiber::MainFunc() {
    try {
        t_fiber->cb_();
        t_fiber->cb_ = nullptr;
        t_fiber->state_ = TERM;
    } catch (std::exception& ex) {
        t_fiber->state_ = EXCEPT;
        FISHER_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
            << " fiber_id=" << t_fiber->getId()
            << std::endl;
            //<< fisher::BacktraceToString();
    }
    t_fiber->yeild();
    assert(false);
}

}