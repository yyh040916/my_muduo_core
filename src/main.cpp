#include "Acceptor.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Logger.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <unistd.h>
#include <sys/socket.h>

/// 用 getsockname 得到本端地址（与 TcpServer::newConnection 相同做法）
static InetAddress getLocalAddr(int sockfd)
{
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t len = sizeof(local);
    if (::getsockname(sockfd, reinterpret_cast<sockaddr *>(&local), &len) < 0)
    {
        LOG_ERROR("getsockname failed");
    }
    return InetAddress(local);
}

int main()
{
    const uint16_t port = 19191;
    EventLoop loop;

    // 保存活跃连接，close 时 erase（完整 TcpServer 用 map + removeConnection）
    std::map<std::string, TcpConnectionPtr> connections;

    Acceptor acceptor(&loop, InetAddress(port, "0.0.0.0"), false);

    acceptor.setNewConnectionCallback(
        [&](int connfd, const InetAddress &peerAddr) {
            // ----- 以下仿 TcpServer::newConnection（单线程版，都在同一个 loop）-----

            char buf[64];
            snprintf(buf, sizeof(buf), "conn#%d", connfd);
            std::string connName = buf;

            InetAddress localAddr = getLocalAddr(connfd);

            // 必须用 shared_ptr：enable_shared_from_this / tie / 回调里 TcpConnectionPtr
            TcpConnectionPtr conn(new TcpConnection(
                &loop, connName, connfd, localAddr, peerAddr));

            connections[connName] = conn;

            // 连接建立/断开（这里只打印；connected() 在 DOWN 时为 false）
            conn->setConnectionCallback([](const TcpConnectionPtr &c) {
                if (c->connected())
                {
                    LOG_INFO("Connection UP %s", c->name().c_str());
                }
                else
                {
                    LOG_INFO("Connection DOWN %s", c->name().c_str());
                }
            });

            // 收到数据：echo 回去（与 muduo testserver 相同写法）
            conn->setMessageCallback(
                [](const TcpConnectionPtr &c, Buffer *buf, Timestamp) {
                    std::string msg = buf->retrieveAllAsString();
                    LOG_INFO("recv from %s: %s", c->peerAddress().toIpPort().c_str(), msg.c_str());
                    c->send(msg);
                });

            // 关闭时从 map 删除（完整版还会 connectDestroyed）
            conn->setCloseCallback([&](const TcpConnectionPtr &c) {
                LOG_INFO("CloseCallback %s", c->name().c_str());
                connections.erase(c->name());
            });

            // 必须在 IO 线程里：tie + enableReading + connectionCallback
            loop.runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
        });

    acceptor.listen();

    std::printf("Echo server on 0.0.0.0:%u — use: nc 127.0.0.1 %u\n",
                static_cast<unsigned>(port), static_cast<unsigned>(port));

    loop.loop();  // 阻塞：accept -> handleRead -> newConnection 回调 -> poll -> handleRead -> echo

    std::puts("EventLoop quit, main exit.");
    return 0;
}