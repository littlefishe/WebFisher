#pragma once

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include "fiber.h"

namespace fisher {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class Scheduler {
public:
    using SchedulerRef = std::shared_ptr<Scheduler>;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] name 协程调度器名称
     */
    Scheduler(size_t threads = 1, const std::string& name = "default sched");

    /**
     * @brief 析构函数
     */
    virtual ~Scheduler();

    /**
     * @brief 返回协程调度器名称
     */
    const std::string& getName() const { return name_;}

    /**
     * @brief 返回当前协程调度器
     */
    static Scheduler* GetThis();

    /**
     * @brief 返回当前协程调度器的调度协程
     */
    static Fiber::FiberRef GetMainFiber();

    /**
     * @brief 启动协程调度器
     */
    void start();

    /**
     * @brief 停止协程调度器
     */
    void stop();

    /**
     * @brief 调度协程
     * @param[in] fc 协程或函数
     * @param[in] thread 协程执行的线程id,-1标识任意线程
     */
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            std::unique_lock ul(latch_);
            need_tickle = scheduleNoLock(fc, thread);
        }
        
        if(need_tickle) {
            tickle();
        }
    }

    std::ostream& dump(std::ostream& os);

    static uint64_t GetThreadId() ;

    static std::string GetThreadName();
    
protected:
    /**
     * @brief 通知协程调度器有任务了
     */

    virtual void tickle();
    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 协程无任务可调度时执行idle协程
     */
    virtual void idle();

    /**
     * @brief 设置当前的协程调度器
     */
    void setThis();

    /**
     * @brief 是否有空闲线程
     */
    bool hasIdleThreads() { return n_idle_thread_ > 0;}
private:
    /**
     * @brief 协程调度启动(无锁)
     */
    bool scheduleNoLock(Fiber::FiberRef fbr, int thread) {
        bool need_tickle = fiber_list_.empty();
        fiber_list_.push_back(fbr);
        return need_tickle;
    }

    bool scheduleNoLock(std::function<void()> cb, int thread) {
        bool need_tickle = fiber_list_.empty();
        fiber_list_.emplace_back(std::make_shared<Fiber>(cb));
        return need_tickle;
    }

    /// 线程池
    std::vector<std::thread> threadpool_;
    /// 待执行的协程队列
    std::list<Fiber::FiberRef> fiber_list_;
    /// 协程调度器名称
    std::string name_;

protected:
    /// 线程数量
    size_t n_thread_ = 0;
    /// 工作线程数量
    std::atomic<size_t> n_active_thread_ = 0;
    /// 空闲线程数量
    std::atomic<size_t> n_idle_thread_ = 0;
    /// 是否正在停止
    bool try_stop_ = false;
    /// 是否自动停止
    bool auto_stop_ = false;

    std::mutex latch_;
};


}
