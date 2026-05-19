#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "noncopyable.h"

class EventLoop;
class EventLoopThread;

/**
 * EventLoopThreadPool：IO 线程池 = 多个 EventLoop（每个线程一个 loop）。
 *
 * 典型用法（与 TcpServer 一致）：
 *   EventLoopThreadPool pool(mainLoop, "Server");
 *   pool.setThreadNum(3);           // 3 个 sub IO 线程（不含 main）
 *   pool.start(initCallback);       // 创建并 startLoop 每个 EventLoopThread
 *   EventLoop *io = pool.getNextLoop();  // 新连接轮询绑定到某个 subLoop
 *
 * numThreads_ == 0：
 *   不创建 sub 线程，getNextLoop() 始终返回 baseLoop_（单线程 Reactor）。
 */
class EventLoopThreadPool : noncopyable
{
public:
    /// 与 EventLoopThread 相同：每个 IO 线程 loop 创建后、loop() 前调用
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    /**
     * @param baseLoop  主线程 EventLoop 指针（accept 所在），不能为空
     * @param nameArg   池名字，用于生成子线程名，如 "Server0"、"Server1"
     */
    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
    ~EventLoopThreadPool();

    /// 设置 sub 线程数量；须在 start() 之前调用
    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    /**
     * 创建 numThreads_ 个 EventLoopThread，各自 startLoop() 进 loops_。
     * 若 numThreads_==0 且 cb 非空，只对 baseLoop_ 调一次 cb（单线程模式初始化）。
     */
    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    /**
     * 轮询返回下一个用于处理连接的 EventLoop。
     * loops_ 为空 → 返回 baseLoop_；
     * 否则 → loops_[next_++]，到末尾回到 0。
     */
    EventLoop *getNextLoop();

    /**
     * 返回池中所有「用于 IO 的」EventLoop 指针。
     * 无 sub 线程时返回只含 baseLoop_ 的 vector(1)；
     * 有 sub 时返回 loops_（不含 baseLoop_，与 muduo 一致）。
     */
    std::vector<EventLoop *> getAllLoops();

    bool started() const { return started_; }
    const std::string name() const { return name_; }

private:
    EventLoop *baseLoop_;   /// 主 loop，accept 用
    std::string name_;      /// 池名，拼子线程名
    bool started_;          /// 是否已 start()
    int numThreads_;        /// sub 线程个数
    int next_;              /// getNextLoop 轮询下标

    std::vector<std::unique_ptr<EventLoopThread>> threads_;  /// 拥有每个 IO 线程对象
    std::vector<EventLoop *> loops_;  /// 指向各 sub 线程里 EventLoop（不拥有，生命周期在线程栈上）
};