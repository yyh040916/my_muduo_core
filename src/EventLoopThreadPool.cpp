#include <memory>
#include <stdio.h>  // snprintf

#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include "EventLoop.h"
#include "Logger.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop)
    , name_(nameArg)
    , started_(false)
    , numThreads_(0)   // 默认 0：单线程，只用 baseLoop_
    , next_(0)         // 轮询从 0 开始
{
    // baseLoop 由外部（main/TcpServer）管理，池子不 delete
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // loops_ 里是指向子线程栈上 EventLoop 的裸指针；
    // 先由各 EventLoopThread 析构时 quit+join，再销毁 threads_，无需 delete loop
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;

    // ----- 创建 numThreads_ 个 sub IO 线程 -----
    for (int i = 0; i < numThreads_; ++i)
    {
        // 线程名：池名 + 下标，例如 "EchoServer0"
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);

        // 每个 EventLoopThread：内部 Thread + 子线程 threadFunc → loop.loop()
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));

        // startLoop() 阻塞到子线程 EventLoop 就绪，返回 * 指针存入 loops_
        loops_.push_back(t->startLoop());
    }

    // ----- 单线程模式：没有 sub 线程，用 baseLoop 跑全部 IO -----
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);  // 仍在 main 线程，给调用方一次初始化 hook
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    // 默认：单线程或未 start 时，所有连接都在 baseLoop_
    EventLoop *loop = baseLoop_;

    // 有 sub 线程时，按 next_ 轮询选一个 subLoop
    if (!loops_.empty())
    {
        loop = loops_[next_];
        ++next_;
        if (next_ >= static_cast<int>(loops_.size()))
        {
            next_ = 0;  // 回到第一个，公平分摊连接
        }
    }

    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        // 没有 sub：整个服务端就一个 loop
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        // 有 sub：只列 sub 的 loop（accept 仍在 baseLoop_ 上）
        return loops_;
    }
}