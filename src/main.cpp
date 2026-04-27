#include "CurrentThread.h"

#include <cstdio>
#include <thread>

int main()
{
    std::printf("main thread tid = %d\n", CurrentThread::tid());
    std::printf("main again      = %d (应相同，走缓存)\n", CurrentThread::tid());

    std::thread t([]() {
        std::printf("child thread tid = %d\n", CurrentThread::tid());
        std::printf("child again      = %d\n", CurrentThread::tid());
    });

    t.join();

    std::puts("若 main 与 child 的 tid 不同，说明各线程缓存独立，行为正常。");
    return 0;
}