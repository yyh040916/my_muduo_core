#include "TcpServer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logger.h"
#include "Buffer.h"
#include "TcpConnection.h"

#include <functional>
#include <string>

/**
 * EchoServer：演示 TcpServer 的典型用法。
 *
 * 流程：
 *   main 里 EventLoop::loop() 转圈 → accept 在 main
 *   setThreadNum(3) → 3 个 sub 线程处理连接读写
 *   收到什么 send 回去什么（echo）
 */
class EchoServer
{
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // ---------- 注册 TcpServer 级回调（会转发给每条 TcpConnection）----------

        // 连接 UP/DOWN 时打印（connected() 为 true/false 区分）
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));

        // 有数据可读：从 Buffer 取出字符串，原样发回
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this,
                      std::placeholders::_1,
                      std::placeholders::_2,
                      std::placeholders::_3));

        // 3 个 sub IO 线程（不含 main）；改成 0 则单线程，accept 和 IO 都在 main
        server_.setThreadNum(3);
    }

    /// 启动监听 + 线程池（listen 在 runInLoop 里调）
    void start()
    {
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp)
    {
        std::string msg = buf->retrieveAllAsString();
        LOG_INFO("echo recv from %s: %s",
                 conn->peerAddress().toIpPort().c_str(), msg.c_str());
        conn->send(msg);
        // conn->shutdown();  // 若打开：半关闭写端，触发关闭流程
    }

    TcpServer server_;   /// 核心服务器对象
    EventLoop *loop_;    /// mainLoop，下面 loop_->loop() 阻塞在此
};

int main()
{
    EventLoop loop;                      // mainReactor
    InetAddress addr(8080, "0.0.0.0");   // 监听所有网卡 8080；仅本机可用 127.0.0.1

    EchoServer server(&loop, addr, "EchoServer");

    server.start();   // 线程池 + Acceptor::listen

    // main 线程进入事件循环：处理 accept；sub 线程各自 loop 处理连接
    loop.loop();

    return 0;
}