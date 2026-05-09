#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

/**
 * Channel::index_ 在 Poller 里的状态机（与 muduo 一致）：
 * kNew    ：从未加入 / 已彻底移除，等待 EPOLL_CTL_ADD
 * kAdded  ：已在 epoll 中注册
 * kDeleted：已从 epoll 删除（events 清空触发 DEL），channels_ 里可能仍保留至下次 ADD
 */
 const int kNew = -1;
 const int kAdded = 1;
 const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    //epoll_create1：创建一个epoll实例，返回一个文件描述符
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC)) // EPOLL_CLOEXEC：exec 其它程序时自动关闭，避免 fd 泄漏到子进程
    , events_(kInitEventListSize) // vector<epoll_event>(16)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_); // 关闭epoll实例
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    //__FUNCTION__：获取当前函数名
    LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

    // 阻塞等待：就绪事件写入 events_[0..numEvents-1]
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno; // epoll_wait 失败时 errno 可能被后续调用改掉，先保存
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happend\n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        // 若就绪数和缓冲区一样大，下次可能不够，扩容以免丢事件边缘情况（内核一次返回多个）
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        // numEvents < 0：出错；EINTR 表示被信号打断，可忽略
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error!");
        }
    }
    return now;//now：当前时间
}

void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();//获取channel的index
    LOG_INFO("func=%s => fd=%d events=%d index=%d\n",
        __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted)//channel未加入或已删除
    {
        if (index == kNew)//channel未加入
        {
            int fd = channel->fd();//获取channel的fd
            channels_[fd] = channel;//将channel添加到channels_中
        }
        // kDeleted -> ADD：表示重新监听
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);//将channel添加到epoll中
    }
    else //channel已加入
    {
         // 已在 poller 中：可能是 MOD，也可能事件清空 -> DEL
         if (channel->isNoneEvent())
         {
             update(EPOLL_CTL_DEL, channel);
             channel->set_index(kDeleted);
         }
         else
         {
             update(EPOLL_CTL_MOD, channel);
         }
    }
}

void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);
    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);
    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        // data.ptr 在 update() 里被设为 Channel*
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    ::memset(&event, 0, sizeof(event));
    int fd = channel->fd();
    event.events = channel->events(); // EPOLLIN | EPOLLOUT 等
    // union：不要同时写 data.fd 与 data.ptr；这里用 ptr 带回 Channel*
    event.data.ptr = channel;
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}