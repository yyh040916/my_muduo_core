#pragma once

#include <vector>
#include <unordered_map>
#include "noncopyable.h"
#include "Timestamp.h"

class Channel;
class EventLoop;

/**
 * Poller：IO 多路复用抽象基类（muduo 里对应 epoll/poll 的统一门面）。
 *
 * 典型使用者是 EventLoop：
 *   poller_->poll(timeoutMs, &activeChannels);
 *   foreach ch in activeChannels: ch->handleEvent(time);
 *
 * 本类不实现 poll()/updateChannel()/removeChannel()，由子类 EPollPoller 等实现。
 */
class Poller : noncopyable
{
public:
    //ChannelList是一个Channel指针的向量
    //本轮 epoll_wait/poll 返回后，所有「有事件」的 Channel 指针放进这个 vector
    using ChannelList = std::vector<Channel *>;

    explicit Poller(EventLoop *loop);

    //析构函数
    virtual ~Poller() = default;

    /**
     * 阻塞等待 fd 就绪。
     * timeoutMs：毫秒；-1 表示一直阻塞直到有事件（具体语义由子类实现）。
     * activeChannels：输出参数，由子类填入就绪的 Channel*。
     * 返回值：poll 返回时刻的时间戳（muduo 用来记日志、传给回调）。
     */
     virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;

    /// 当 Channel 上关心的事件变化时调用：子类内部 epoll_ctl ADD/MOD
    virtual void updateChannel(Channel *channel) = 0;

    /// 从多路复用里摘掉某个 Channel：子类内部 epoll_ctl DEL，并维护 channels_
    virtual void removeChannel(Channel *channel) = 0;

    /**
     * 判断 channel 是否仍在本 Poller 的登记表里，
     * 且 fd 映射到的指针确实是这个 channel（防止 fd 复用后误匹配）。
     */
    bool hasChannel(Channel *channel) const;

     /**
     * 工厂：创建「默认」的具体 Poller（Linux 下一般是 EPollPoller）。
     * 定义写在单独的 DefaultPoller.cc，避免 Poller.h 依赖 EPollPoller.h 形成循环包含。
     */
    static Poller *newDefaultPoller(EventLoop *loop);

protected:
    /// map的key:sockfd value:sockfd所属的channel通道类型
    /// key：sockfd；value：负责该 fd 的 Channel*
    /// 子类在 ADD 时插入，DEL 时 erase，用于调试与 hasChannel。
    using ChannelMap = std::unordered_map<int, Channel *>;
    ChannelMap channels_;

private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
};