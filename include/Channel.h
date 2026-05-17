#pragma once

#include <functional>  // std::function：保存读/写/关闭等回调
#include <memory>        // std::weak_ptr / std::shared_ptr（tie 用）

#include "noncopyable.h"
#include "Timestamp.h"

// 前向声明：本头文件里只有 EventLoop* 指针，不需要包含 EventLoop.h，减少编译依赖
class EventLoop;

/**
 * Channel：Reactor 模型里「一个 fd 对应一条事件通道」。
 *
 * 和 Poller、EventLoop 的关系：
 *   - Poller（epoll）只认识 fd 和 EPOLLIN/OUT 等内核事件
 *   - Channel 把 fd 包起来，并挂上 C++ 回调（读到了怎么办、可写了怎么办）
 *   - EventLoop::loop() 在 poll 返回后，对每个就绪 Channel 调用 handleEvent()
 *
 * 两个「事件」字段要分清：
 *   events_  ：你向 epoll **注册**关心什么（enableReading 改这个）
 *   revents_ ：epoll_wait **返回**时实际发生了什么（EPollPoller::fillActiveChannels 写入）
 */
class Channel : noncopyable
{
public:
    // 无参 void 回调：写完成、关闭、错误等
    using EventCallback = std::function<void()>;

    // 读回调多带一个 Timestamp：表示本轮事件到达/处理的时间点
    using ReadEventCallback = std::function<void(Timestamp)>;

    /**
     * loop：该 Channel 归属哪个 EventLoop（One Loop Per Thread）
     * fd  ：要监听的文件描述符（socket、pipe、eventfd 等）
     */
    Channel(EventLoop *loop, int fd);
    ~Channel();

    /**
     * 由 EventLoop 在 poll 之后调用。
     * receiveTime：通常传 Poller::poll 返回的 Timestamp::now()
     */
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉 channel还在执行回调操作
    /**
     * tie：把 Channel 与 TcpConnection 的 shared_ptr 绑在一起。
     * handleEvent 里先 tie_.lock()，失败说明连接对象已销毁，不再执行回调。
     * （完整 TcpConnection 写好后再用；测试阶段可不调用 tie）
     */
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    // 设置fd相应的事件状态 相当于epoll_ctl add delete
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    // index_：在 EPollPoller 里的状态机，初值 -1（kNew）
    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop *ownerLoop() { return loop_; }
    void remove();
private:

    /// 把当前 events_ 同步到 epoll：loop_->updateChannel(this)
    void update();
    /// 真正根据 revents_ 分发回调（handleEvent 在 tie 检查之后调用）
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_;   // 所属事件循环
    const int fd_;      // 监听的 fd，生命周期内不变
    int events_;        // 关心的事件（注册到 epoll）
    int revents_;       // 实际就绪的事件（poll 后填写）
    int index_;         // Poller 内部状态：kNew(-1) / kAdded(1) / kDeleted(2)
    
    std::weak_ptr<void> tie_;  // 不增加 TcpConnection 引用计数，避免循环引用
    bool tied_;                // 是否调用过 tie()

    // 因为channel通道里可获知fd最终发生的的具体事件events，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};