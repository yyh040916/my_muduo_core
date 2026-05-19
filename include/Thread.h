#pragma once

#include <functional>   // std::function：线程要执行的函数类型
#include <thread>         // 底层真正创建 OS 线程
#include <memory>         // std::shared_ptr<std::thread>
#include <unistd.h>       // pid_t（Linux 上 tid 常用 pid_t 表示）
#include <string>
#include <atomic>         // numCreated_ 多线程安全计数

#include "noncopyable.h"  // 禁止拷贝/赋值

/**
 * Thread：muduo 对「一条工作线程」的封装。
 *
 * 用法典型流程：
 *   Thread t([]{ ... 在线程里干活 ... }, "IO-1");
 *   t.start();   // 创建并运行线程，且 start() 返回时 tid_ 已有效
 *   t.join();    // 等待线程结束
 *
 * 注意：
 *   - start() 之前不能 join()
 *   - start() 之后若既不 join 也不等线程结束就析构，会 detach（见析构函数）
 */
class Thread : noncopyable
{
public:
    /// 线程入口：无参、无返回值，用 lambda 包一层即可
    using ThreadFunc = std::function<void()>;

    /**
     * @param func  在新线程里执行的函数（会被 move 进成员）
     * @param name  线程名，用于日志；空则自动生成 Thread1、Thread2...
     */
    explicit Thread(ThreadFunc func, const std::string &name = std::string());
    ~Thread();

    /// 创建并启动底层 std::thread；阻塞直到子线程写好 tid_
    void start();

    /// 等待线程结束（只能 join 一次）
    void join();

    bool started() { return started_; }
    /// 子线程的内核 TID（start() 返回后才有意义）
    pid_t tid() const { return tid_; }

    /// 线程名，用于日志
    const std::string &name() const { return name_; }

    /// 全局统计：本进程里通过 Thread 类创建过多少个线程对象（构造时 +1）
    static int numCreated() { return numCreated_; }
    
private:
    void setDefaultName();  // name 为空时赋默认名 ThreadN
    bool started_;                          // 是否已 start()
    bool joined_;                           // 是否已 join()
    std::shared_ptr<std::thread> thread_;   // 底层线程句柄
    pid_t tid_;                             // 子线程 tid，在子线程 lambda 里赋值
    ThreadFunc func_;                       // 用户传入的线程函数
    std::string name_;
    static std::atomic_int numCreated_;     // 静态成员，类外定义
};