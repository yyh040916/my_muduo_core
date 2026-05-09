#include <stdlib.h> // getenv

#include "Poller.h"
#include "EPollPoller.h"

/**
 * Poller::newDefaultPoller 的实现放在此文件：
 * - 避免 Poller.h 直接 #include EPollPoller.h，减小头文件依赖与编译耦合。
 *
 * 默认：Linux 使用 EPollPoller。
 * 若环境变量 MUDUO_USE_POLL 存在：返回 nullptr，表示「应换用 poll 实现」；
 * 教学代码未实现 PollPoller 时，不要设置该环境变量。
 */
Poller *Poller::newDefaultPoller(EventLoop *loop)
{
    if (::getenv("MUDUO_USE_POLL"))//getenv：获取环境变量
    {
        return nullptr;
    }
    else
    {
        return new EPollPoller(loop);
    }
}