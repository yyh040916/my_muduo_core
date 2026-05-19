#pragma once

#include <functional>           // ThreadInitCallback
#include <mutex>                // 保护 loop_ 指针的可见性
#include <condition_variable>   // startLoop() 等待子线程创建好 loop
#include <string>

#include "noncopyable.h"
#include "Thread.h"             // 底层用 Thread 起子线程

class EventLoop;  // 前向声明，.h 里不必 include EventLoop.h


/**
 * EventLoopThread：一条专用 IO 线程 + 一个 EventLoop。
 *
 * 典型用法：
 *   EventLoopThread loopThread;
 *   EventLoop *loop = loopThread.startLoop();  // 阻塞到子线程 loop 就绪
 *   loop->runInLoop(...);   // 从别的线程往 IO 线程丢任务
 *   // 析构 loopThread 时自动 quit + join
 *
 * 可选 ThreadInitCallback：
 *   在子线程里、loop.loop() 之前执行，用于初始化 TLS、注册定时器等。
 */
class EventLoopThread : noncopyable
{
public:
    /// 子线程里 EventLoop 创建后、进入 loop() 之前调用；参数是该线程的 loop
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    /**
     * @param cb    线程初始化回调，可传空（默认构造的空 function）
     * @param name  传给底层 Thread 的线程名，便于日志
     */
     EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
     const std::string &name = std::string());
    ~EventLoopThread();

    /**
     * 启动子线程，并阻塞直到子线程里的 EventLoop 对象已创建且 loop_ 已赋值。
     * @return 指向子线程栈上 EventLoop 的指针（子线程 threadFunc 结束前有效）
     */
     EventLoop *startLoop();

private:
    /// 在子线程中执行：构造 EventLoop → 通知主线程 → loop.loop()
    void threadFunc();

    EventLoop *loop_;              /// 指向子线程栈上的 EventLoop；未就绪时为 nullptr
    bool exiting_;                 /// 析构标志，避免重复 quit
    Thread thread_;                /// 封装 std::thread，线程函数是 threadFunc
    std::mutex mutex_;             /// 配合 cond_，保护 loop_ 的读写
    std::condition_variable cond_; /// startLoop 等待 loop_ != nullptr
    ThreadInitCallback callback_;  /// 可选：loop 创建后、loop() 前的钩子
};