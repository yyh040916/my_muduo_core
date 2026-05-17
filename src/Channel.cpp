#include <sys/epoll.h>  // EPOLLIN / EPOLLOUT / EPOLLHUP / EPOLLERR 等

#include "Channel.h"
#include "EventLoop.h"  // update() / remove() 需要 EventLoop 声明
#include "Logger.h"

// 静态成员必须在 .cpp 里定义（链接时需要实体）
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;  // 可读 + 带外数据（一般也用可读处理）
const int Channel::kWriteEvent = EPOLLOUT;         // 可写

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{
}

Channel::~Channel()
{
}

/**
 * tie：把 Channel 与 TcpConnection 的 shared_ptr 绑在一起。
 * handleEvent 里先 tie_.lock()，失败说明连接对象已销毁，不再执行回调。
 * （完整 TcpConnection 写好后再用；测试阶段可不调用 tie）
 */
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;       // weak_ptr 从 shared_ptr 构造，不增加引用计数
    tied_ = true;
}

/**
 * 当改变channel所表示的fd的events事件后，update负责再poller里面更改fd相应的事件epoll_ctl
 */
void Channel::update()
{
    // 通过channel所属的eventloop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

// 在channel所属的EventLoop中把当前的channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

/**
 * 由 EventLoop 在 poll 之后调用。
 * receiveTime：通常传 Poller::poll 返回的 Timestamp::now()
 */
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        // 提升 weak_ptr；若 TcpConnection 已析构，lock() 得到空，直接跳过
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)//如果guard不为空，则调用handleEventWithGuard
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        // 未 tie（测试或未绑定连接对象）时直接处理
        handleEventWithGuard(receiveTime);
    }
}

/**
 * 真正根据 revents_ 分发回调（handleEvent 在 tie 检查之后调用）
 */
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n", revents_);
    /**
     * 顺序有讲究（与 muduo 原版一致）：
     * 1) HUP 且不可读：常表示对端 shutdown 写端，走 close
     * 2) ERR
     * 3) IN / PRI：读
     * 4) OUT：写
     * 同一轮可能多个分支都满足，都会执行（例如既 ERR 又 IN）
     */
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }
    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}
