#include <sys/eventfd.h>  // eventfd、EFD_NONBLOCK、EFD_CLOEXEC
#include <unistd.h>       // read、write、close
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"

/**
 * 线程局部变量：指向「当前线程」正在使用的 EventLoop。
 * __thread：每个线程一份，互不干扰。
 * 构造 EventLoop 时若已非空，说明同线程创建了第二个 EventLoop，直接 FATAL。
 */
__thread EventLoop *t_loopInThisThread = nullptr;

/// epoll_wait 最长阻塞时间（毫秒）；超时后仍会返回，便于处理 pending 等
const int kPollTimeMs = 10000;

/**
 * 创建 eventfd。
 * - initval=0：初始计数为 0
 * - EFD_NONBLOCK：read/write 不阻塞
 * - EFD_CLOEXEC：exec 时自动关闭，防泄漏到子进程
 */
 static int createEventfd()
 {
     int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);//eventfd：创建一个eventfd文件描述符
     if (evtfd < 0)
     {
         LOG_FATAL("eventfd error:%d\n", errno);
     }
     return evtfd;
 }

 EventLoop::EventLoop()
 : looping_(false)                              // 尚未进入 loop()
 , quit_(false)                                 // 未请求退出
 , threadId_(CurrentThread::tid())              // 记下「我是哪个线程的 loop」
 , poller_(Poller::newDefaultPoller(this))       // 工厂创建 EPollPoller（顺序须与类内成员声明一致）
 , wakeupFd_(createEventfd())                   // 用于线程间唤醒
 , wakeupChannel_(new Channel(this, wakeupFd_)) // 用 Channel 包装 wakeupFd_，走统一 epoll 路径
 , callingPendingFunctors_(false)               // 未在执行 pending（在 poller_ 之后声明，故放后面初始化）
{
 LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
 // 同线程只能有一个 EventLoop
 if (t_loopInThisThread)
 {
     LOG_FATAL("Another EventLoop %p exists in this thread %d\n",
               t_loopInThisThread, threadId_);
 }
 else
 {
     t_loopInThisThread = this;
 }
 // eventfd 可读时：在 IO 线程执行 handleRead，把计数读走
 wakeupChannel_->setReadCallback(
     std::bind(&EventLoop::handleRead, this));
 // 把 wakeupFd_ 注册进 epoll，关心 EPOLLIN
 wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    // 析构顺序：先从 epoll 摘掉 wakeupChannel_，再 close fd
    wakeupChannel_->disableAll();  // events_=0 -> epoll_ctl DEL
    wakeupChannel_->remove();      // 从 channels_ 删除
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;  // 本线程不再有关联的 loop
}

/**
 * 启动事件循环；通常只在创建 EventLoop 的那条线程里调用一次
 */
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping\n", this);
    // 反应堆主循环：直到 quit_ 为 true
    while (!quit_)
    {
        // 每轮重新收集就绪 Channel，避免沿用上一轮指针
        activeChannels_.clear();
        // 阻塞在 epoll_wait（最多 kPollTimeMs）；返回时填 activeChannels_
        pollRetureTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        // 分发：根据 revents_ 调 read/write/close/error 回调
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(pollRetureTime_);
        }
        // 执行其他线程通过 queueInLoop 投递的回调（如 subLoop 上处理新连接）
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop looping.\n", this);
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;  // 下一轮 while (!quit_) 退出
    // 若在其他线程调 quit，IO 线程可能正卡在 epoll_wait，必须 wakeup
    if (!isInLoopThread())
    {
        wakeup();
    }
}

/**
 * 在「所属 IO 线程」执行 cb
 * 若当前线程就是 IO 线程：直接 cb()。
 * 否则：转 queueInLoop(cb)，由 IO 线程稍后执行。
 */
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 当前EventLoop中执行回调
    {
        cb();
    }
    else // 在非当前EventLoop线程中执行cb，就需要唤醒EventLoop所在线程执行cb
    {
        queueInLoop(cb);
    }
}

/**
 * 把 cb 追加到 pendingFunctors_（加锁），必要时 wakeup。
 * 真正执行在 loop() 每轮末尾的 doPendingFunctors()。
 */
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }
    /**
     * 两种情况要 wakeup：
     * 1) 调用者不是 IO 线程 -> IO 线程可能阻塞在 poll，必须唤醒
     * 2) 正在 doPendingFunctors 里，又 queue 了新任务 -> 唤醒下一轮 poll，避免等太久
     */
     if (!isInLoopThread() || callingPendingFunctors_)
     {
         wakeup();
     }
}

/**
 * wakeupChannel_ 触发 EPOLLIN 时调用：read eventfd，清空通知
 */
void EventLoop::handleRead()
{
    // eventfd 每次 write 使计数 +1；read 读走 8 字节，计数归零，避免重复触发 EPOLLIN
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() reads %ld bytes instead of 8\n",
                  static_cast<long>(n));
    }
}

/**
 * 通过 eventfd 唤醒 loop() 所在的线程（通常是 IO 线程）
 */
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %ld bytes instead of 8\n",
                  static_cast<long>(n));
    }
}

void EventLoop::updateChannel(Channel *channel)
{
    // Channel::enableReading 等最终会走到这里
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

/**
 * 交换并执行 pendingFunctors_ 里的所有 Functor
 */
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;//存储待执行的回调函数
    callingPendingFunctors_ = true;//标识当前loop是否有需要执行的回调操作
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // swap 把 pending 一次性换出，缩短持锁时间；
        // 且避免在持锁时执行 functor（若 functor 里再 queueInLoop 可能死锁）
        functors.swap(pendingFunctors_);
    }
    for (const Functor &functor : functors)
    {
        functor();//执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}