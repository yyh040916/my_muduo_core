#pragma once
#include <unistd.h>//获取进程id
#include <sys/syscall.h>//获取线程id

/**
 * CurrentThread：与「当前正在执行的线程」相关的工具，主要是获取并缓存 tid。
 *
 * Linux 下用 syscall(SYS_gettid) 取内核线程 ID（gettid），与 pthread_t 不是同一类型。
 * 使用 __thread 让每个线程有自己独立的缓存变量，避免多线程互相覆盖。
 */
namespace CurrentThread
{
    /// 线程局部缓存：当前线程的 tid；0 表示尚未缓存，第一次访问时再 syscall
    extern __thread int t_cachedTid;//extern __thread 表示线程局部变量 每个线程有自己独立的缓存变量
    /// 通过系统调用填充 t_cachedTid（仅在未缓存时调用）
    void cacheTid();

    /// 获取当前线程的 tid；若未缓存，则调用 cacheTid() 获取并缓存
    inline int tid()//内联函数只在当前文件中起作用
    {
        if (__builtin_expect(t_cachedTid == 0, 0))//__builtin_expect 是一种底层优化 此语句意思是如果还未获取tid 进入if 通过cacheTid()系统调用获取tid
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}