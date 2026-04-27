#include "CurrentThread.h"

namespace CurrentThread
{
    /// 每个线程独立一份，初始为 0 表示「尚未调用 cacheTid」
    __thread int t_cachedTid = 0;

    void cacheTid()
    {
        if (t_cachedTid == 0)
        {
            // SYS_gettid：返回调用线程在内核中的 TID（在 Linux 上常用于区分线程）
            // static_cast<int> 与头文件中 int 缓存类型一致
            t_cachedTid=static_cast<int>(static_cast<pid_t>(::syscall(SYS_gettid)));
        }
    }
}