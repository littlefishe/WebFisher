#pragma once

#include <memory>
#include <functional>
#include <ucontext.h>

namespace fisher {

class Scheduler;

/**
 * @brief 协程类
 */
class Fiber : public std::enable_shared_from_this<Fiber> {
friend class Scheduler;
public:
    using FiberRef = std::shared_ptr<Fiber>;

    /**
     * @brief 协程状态
     */
    enum State {
        /// 初始化状态
        INIT,
        /// 暂停状态
        HOLD,
        /// 执行中状态
        EXEC,
        /// 结束状态
        TERM,
        /// 可执行状态
        READY,
        /// 异常状态
        EXCEPT
    };
private:
    /**
     * @brief 无参构造函数
     * @attention 每个线程第一个协程的构造
     */
    Fiber();

public:
    /**
     * @brief 构造函数
     * @param[in] cb 协程执行的函数
     * @param[in] stacksize 协程栈大小
     * @param[in] use_caller 是否在MainFiber上调度
     */
    Fiber(std::function<void()> cb, size_t stacksize = 0);

    /**
     * @brief 析构函数
     */
    ~Fiber();

    /**
     * @brief 重置协程执行函数,并设置状态
     * @pre getState() 为 INIT, TERM, EXCEPT
     * @post getState() = INIT
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 将当前协程切换到运行状态
     * @pre getState() != EXEC
     * @post getState() = EXEC
     */
    void call();

    /**
     * @brief 将当前协程切换到后台
     */
    void yeild();

    /**
     * @brief 将当前线程切换到执行状态
     * @pre 执行的为当前线程的主协程
     */

    /**
     * @brief 返回协程id
     */
    uint64_t getId() const { return fid_;}

    /**
     * @brief 返回协程状态
     */
    State getState() const { return state_;}
public:

    /**
     * @brief 设置当前线程的运行协程
     * @param[in] f 运行协程
     */
    static void SetThis(FiberRef f);

    static FiberRef GetThis();

    /**
     * @brief 返回当前协程的总数量
     */
    static uint64_t TotalFibers();

    /**
     * @brief 协程执行函数
     * @post 执行完成返回到线程主协程
     */
    static void MainFunc();

    /**
     * @brief 获取当前协程的id
     */
    static uint64_t GetFiberId();
private:
    uint64_t fid_ = 0;
    uint32_t stacksize_ = 0;
    State state_ = INIT;
    ucontext_t ctx_;
    void* stack_ = nullptr;
    std::function<void()> cb_;
};

}
