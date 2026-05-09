#pragma once

#include <vector>
#include <sys/epoll.h>
#include "Poller.h"
#include "Timestamp.h"

class Channel;

/**
 * EPollPoller：基于 Linux epoll 的 Poller 实现。
 *
 * 使用顺序（概念）：
 *   epoll_create1  -> 得到 epollfd_
 *   epoll_ctl ADD/MOD/DEL  <- updateChannel/removeChannel 内部调用 update()
 *   epoll_wait           <- poll()
 *
 * epoll_event.data.ptr 存放 Channel*：就绪时无需再用 fd 查表即可分发。
 */
class EPollPoller : public Poller 
{
public:
    //构造函数
    explicit EPollPoller(EventLoop *loop);

    //析构函数
    ~EPollPoller() override;
    
    //重写基类Poller的抽象方法
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;
private:
    /// epoll_wait 一次返回的事件个数上限初始值；不够会在 poll 里扩容 events_
    static const int kInitEventListSize = 16;

    /// 把 epoll_wait 得到的 numEvents 个就绪事件翻译成 Channel* 列表
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

    /// 封装 epoll_ctl(operation, fd, &event)，operation 为 EPOLL_CTL_ADD/MOD/DEL
    void update(int operation, Channel *channel);

    /// 用于存放 epoll_wait 返回的所有发生的事件的文件描述符事件集
    using EventList = std::vector<epoll_event>;

    int epollfd_;       /// epoll 实例对应的 fd，析构时要 close
    EventList events_; /// epoll_wait 的输出缓冲区（内核向里填就绪事件）
};