#include "Thread.h"
#include "CurrentThread.h"  // 子线程里取 tid：CurrentThread::tid()

#include <semaphore.h>  // sem_init / sem_post / sem_wait：start() 里同步 tid_

/// 静态成员必须在 .cpp 里定义且只定义一次
std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)                    // 尚未 start，子线程 tid 未知
    , func_(std::move(func))     // 把用户函数存进成员，start 时再执行
    , name_(name)
{
    setDefaultName();  // 若 name 为空 → "Thread1"、"Thread2"...
}

Thread::~Thread()
{
    // 若已 start 但未 join：std::thread 析构会 std::terminate
    // muduo 选择 detach，让线程在后台跑完自己结束（教学代码里建议总是 join）
    if (started_ && !joined_)
    {
        thread_->detach();
    }
}

void Thread::start()
{
    started_ = true;
    // 匿名信号量，初值 0：主线程 sem_wait 会阻塞，直到子线程 sem_post
    sem_t sem;//信号量 用于线程间的同步
    sem_init(&sem, false, 0);  // false = 线程间不用，仅本进程内同步
    // 用 shared_ptr 管理 std::thread（muduo 原版写法；也可 unique_ptr）
    thread_ = std::shared_ptr<std::thread>(new std::thread([&]() {
        // ===== 以下代码在【新线程】里执行 =====
        tid_ = CurrentThread::tid();  // 缓存当前线程的内核 TID
        sem_post(&sem);               // 通知主线程：tid_ 已写好
        func_();                      // 执行用户真正的线程函数
        // ===== 新线程结束 =====
    }));
    // 主线程停在这里，直到子线程 post；保证 start() 返回后 tid() 可用
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();  // 阻塞直到 func_() 跑完
}

/// 设置默认线程名
void Thread::setDefaultName()
{
    int num = ++numCreated_;  // 原子自增，统计创建过的 Thread 个数
    if (name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof buf, "Thread%d", num);
        name_ = buf;
    }
}