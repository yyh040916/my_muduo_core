#include "Acceptor.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logger.h"

#include <cstdio>
#include <thread>
#include <unistd.h>

int main()
{
    // 日志里能看到 Acceptor / Channel / EPollPoller 的输出
    const uint16_t port = 19090;

    EventLoop loop; // 主线程的 IO 循环

    // reuseport 参数此处传 false 即可（与 muduo 构造行为一致）
    Acceptor acceptor(&loop, InetAddress(port, "0.0.0.0"), false);

    acceptor.setNewConnectionCallback(
        [&loop](int connfd, const InetAddress &peer) {
            // 本测试不接 TcpConnection：只打印并关闭连接，然后退出 loop
            std::printf("[main] new connection fd=%d from %s\n",
                        connfd, peer.toIpPort().c_str());
            ::close(connfd);
            loop.quit(); // 结束 loop.loop()，main 才能继续
        });

    acceptor.listen(); // 开始 listen + 把 listenfd 挂到 epoll

    std::printf("Server listening on 0.0.0.0:%u\n", static_cast<unsigned>(port));
    std::printf("In another terminal run: nc 127.0.0.1 %u\n", static_cast<unsigned>(port));

    // 在子线程里 2 秒后自动连一次，避免你一直手动 nc（也可删掉这段改用手动 nc）
    std::thread client([&]() {
        ::sleep(2);
        std::string cmd = "bash -c 'echo test | nc 127.0.0.1 " + std::to_string(port) + "'";
        std::system(cmd.c_str());
    });

    loop.loop(); // 阻塞在此，直到 quit()

    client.join();

    std::puts("Acceptor test done.");
    return 0;
}