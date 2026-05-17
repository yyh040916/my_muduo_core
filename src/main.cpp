#include "EventLoop.h"
#include "Channel.h"
#include "Timestamp.h"

#include <cassert>
#include <cstdio>
#include <thread>
#include <unistd.h>

int main()
{
    // ========== 测试 1：pipe 触发读事件，在回调里 quit ==========
    {
        EventLoop loop;  // 主线程创建 loop，threadId_ 即主线程 tid

        int fds[2];
        assert(::pipe(fds) == 0);
        // fds[0] 读端  fds[1] 写端

        Channel ch(&loop, fds[0]);

        ch.setReadCallback([&](Timestamp) {
            std::puts("[test1] pipe EPOLLIN -> readCallback");
            loop.quit();  // 请求退出；下一轮 while 结束
        });

        ch.enableReading();  // epoll_ctl ADD 读端

        std::thread writer([&] {
            ::sleep(1);           // 先让 loop.loop() 跑起来并阻塞在 poll
            const char c = 'x';
            ::write(fds[1], &c, 1);  // 写 1 字节 -> 读端就绪
        });

        loop.loop();  // 阻塞在此，直到 quit()

        writer.join();
        ::close(fds[0]);
        ::close(fds[1]);
    }

    // ========== 测试 2：子线程 queueInLoop，验证 wakeup + pending ==========
    {
        EventLoop loop;

        std::thread other([&] {
            ::sleep(1);

            // 在 IO 线程打印（通过 pending 队列）
            loop.queueInLoop([]() {
                std::puts("[test2] executed on IO thread");
            });

            // 再在 IO 线程调 quit
            loop.queueInLoop([&]() {
                loop.quit();
            });
        });

        loop.loop();  // IO 线程：poll 被 wakeup 后执行上面两个 functor

        other.join();
    }

    std::puts("EventLoop all tests passed.");
    return 0;
}