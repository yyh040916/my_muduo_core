#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
    const std::string &name)
: loop_(nullptr)   // 子线程还没跑，尚无 EventLoop
, exiting_(false)
// 构造 Thread 时只「登记」线程函数，真正 start 在 startLoop() 里
, thread_(std::bind(&EventLoopThread::threadFunc, this), name)
, mutex_()
, cond_()
, callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    // 若从未 startLoop，loop_ 一直为 nullptr，无需 quit/join
    if (loop_ != nullptr)
    {
        loop_->quit();   // 让子线程里 loop.loop() 的 while 退出（可能 wakeup）
        thread_.join();  // 等 threadFunc 跑完（loop 析构、loop_=nullptr）
    }
}

EventLoop *EventLoopThread::startLoop()
{
    // 1. 启动底层 Thread（内部 sem 保证子线程 tid 已就绪）
    thread_.start();
    EventLoop *loop = nullptr;
    {
        // 2. 主线程加锁，等待子线程在 threadFunc 里创建 EventLoop 并 notify
        std::unique_lock<std::mutex> lock(mutex_);
        // 谓词：防止虚假唤醒；只有 loop_ 非空才继续
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
        loop = loop_;  // 复制指针，返回给调用者
    }
    return loop;
}

// ========== 以下全部在【子线程】中执行 ==========
void EventLoopThread::threadFunc()
{
    // EventLoop 必须在「将要调用 loop() 的线程」里构造（One Loop Per Thread）
    EventLoop loop;
    // 用户钩子：例如打日志、设置线程名、初始化仅本线程可见的数据
    if (callback_)
    {
        callback_(&loop);
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;       // 让主线程 startLoop() 里的 wait 返回
        cond_.notify_one();  // 唤醒一个在 cond_.wait 上阻塞的线程（即主线程）
    }
    loop.loop();  // 阻塞在这里：epoll_wait → handleEvent → doPendingFunctors ...
    // loop.loop() 返回后，EventLoop 即将析构，指针失效，置空
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
}