#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

bool Poller::hasChannel(Channel *channel) const
{
    // 用 fd 在 map 里查找；不仅要存在，还要指针一致（防止同一 fd 号被复用）
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}
// 注意：Poller::newDefaultPoller 的实现放在 DefaultPoller.cpp 里，
// 那里通常会 #include "EPollPoller.h" 并 return new EPollPoller(loop);