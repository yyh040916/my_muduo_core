#pragma once

#include <functional>  // std::function，Functor 类型
#include <vector>        // activeChannels_、pendingFunctors_
#include <atomic>        // looping_、quit_ 等，多线程可见
#include <memory>        // unique_ptr<Poller>、unique_ptr<Channel>
#include <mutex>         // 保护 pendingFunctors_

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;  // 前向声明，避免头文件互相 include
class Poller;

/**
 * EventLoop：一条 IO 线程上的反应堆主循环。
 *
 * 核心循环在 loop()：
 *   poll 等事件 -> 对每个就绪 Channel 调 handleEvent -> doPendingFunctors
 *
 * 设计要点：
 *   - One Loop Per Thread：构造时记录 threadId_，并设置 t_loopInThisThread
 *   - wakeupFd_（eventfd）：别的线程 queueInLoop / quit 时唤醒阻塞在 epoll_wait 的线程
 */
class EventLoop : noncopyable
{
public:
    // 无参 void 的可调用对象，用于跨线程投递任务
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    /// 启动事件循环；通常只在创建 EventLoop 的那条线程里调用一次
    void loop();

    /// 令 loop() 在下一轮 while 判断时退出；可能从其他线程调用
    void quit();

    /// 最近一次 poller_->poll 返回时的时间戳（muduo 成员名拼写为 pollRetureTime_）
    Timestamp pollReturnTime() const { return pollRetureTime_; }

    /**
     * 在「所属 IO 线程」执行 cb。
     * 若当前线程就是 IO 线程：直接 cb()。
     * 否则：转 queueInLoop(cb)，由 IO 线程稍后执行。
     */
    void runInLoop(Functor cb);

    /**
     * 把 cb 追加到 pendingFunctors_（加锁），必要时 wakeup。
     * 真正执行在 loop() 每轮末尾的 doPendingFunctors()。
     */
    void queueInLoop(Functor cb);

    /// 通过 eventfd 唤醒 loop() 所在的线程（通常是 IO 线程）
    void wakeup();

    /// EventLoop 的方法 => Poller 的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    /// 判断是否在 IO 线程中
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
    /// wakeupChannel_ 触发 EPOLLIN 时调用：read eventfd，清空通知
    void handleRead();

    /// 交换并执行 pendingFunctors_ 里的所有 Functor
    void doPendingFunctors();

    using ChannelList = std::vector<Channel *>;

    std::atomic_bool looping_;  /// 是否正在 loop() 中（原子，避免数据竞争）

    std::atomic_bool quit_;     /// 为 true 时 loop() 的 while 结束

    const pid_t threadId_;      /// 构造时 CurrentThread::tid()，标识所属线程

    Timestamp pollRetureTime_;  /// poll 返回时刻，传给 Channel::handleEvent
    
    std::unique_ptr<Poller> poller_;  /// 默认 EPollPoller，负责 epoll_wait
    int wakeupFd_;                          /// eventfd 的 fd
    std::unique_ptr<Channel> wakeupChannel_; /// 监听 wakeupFd_ 可读
    ChannelList activeChannels_;  /// 本轮 poll 就绪的 Channel 列表（每轮 clear）
    std::atomic_bool callingPendingFunctors_;  /// 是否正在执行 pending 回调
    std::vector<Functor> pendingFunctors_;     /// 待执行回调队列
    std::mutex mutex_;                         /// 保护 pendingFunctors_
};