#pragma once
#include <functional>
#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

class EventLoop;
class InetAddress;

/**
 * Acceptor：在 listenfd 上接受新 TCP 连接。
 *
 * 典型用法（在 TcpServer::start 里）：
 *   Acceptor acc(baseLoop, listenAddr, reuseport);
 *   acc.setNewConnectionCallback(...);
 *   acc.listen();  // listen + 把 listenfd 注册进 epoll
 *   baseLoop->loop();
 *
 * 当有新连接：listenfd 可读 -> acceptChannel_ 回调 handleRead -> accept -> 用户回调
 */
class Acceptor : noncopyable
{
public:
    /// 新连接就绪时：参数为已 accept 的 connfd 和对端地址（主机字节序端口在 InetAddress 里）
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;

    /**
     * loop       ：所属的 EventLoop（一般是 mainLoop / baseLoop）
     * listenAddr ：绑定的本地 IP:端口
     * reuseport  ：API 保留；本教学实现里构造时仍会对 socket 设 REUSEADDR/REUSEPORT（与原版一致）
     */
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    /// 注册「新连接」回调；由 TcpServer 传入，内部再创建 TcpConnection
    void setNewConnectionCallback(const NewConnectionCallback &cb)
    {
        NewConnectionCallback_ = cb;
    }

    /// 是否已调用 listen() 并开始 epoll 监听
    bool listenning() const { return listenning_; }

    /// 开始 listen，并把 listenfd 的读事件注册到 Poller（重要：之前不会触发 accept）
    void listen();

private:
    /// listenfd 可读时由 Channel 调用：执行 accept，再调 NewConnectionCallback_
    void handleRead();

    EventLoop *loop_;              /// 所属事件循环
    Socket acceptSocket_;          /// 封装 listenfd（RAII，析构时 close）
    Channel acceptChannel_;        /// 监听 listenfd 上的 EPOLLIN
    NewConnectionCallback NewConnectionCallback_; /// 新连接回调，可为空
    bool listenning_;              /// 是否已进入监听状态
};