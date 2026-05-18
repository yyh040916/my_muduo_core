#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

/**
 * 创建非阻塞的 TCP 监听套接字。
 * SOCK_NONBLOCK：配合 epoll 边缘/水平触发，accept 不会阻塞整个线程
 * SOCK_CLOEXEC：exec 子进程时不继承该 fd
 */
static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

/**
 * 构造顺序说明（成员初始化列表）：
 *   acceptSocket_ 必须先于 acceptChannel_ 建好，因为 acceptChannel_ 要用 acceptSocket_.fd()
 * 类内成员声明顺序也是 acceptSocket_ 在前、acceptChannel_ 在后，与之一致。
 */
 Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
 : loop_(loop)
 , acceptSocket_(createNonblocking())       // 得到 listenfd
 , acceptChannel_(loop, acceptSocket_.fd()) // 用 listenfd 构造 Channel，尚未 enableReading
 , listenning_(false)
{
 (void)reuseport; // 原版 API 有该参数，实现里固定开启下面两个选项
 acceptSocket_.setReuseAddr(true);  // 重启服务时可快速 rebind
 acceptSocket_.setReusePort(true);  // 多进程监听同一端口（Linux）
 acceptSocket_.bindAddress(listenAddr);
 // listenfd 可读 => 有新连接在已完成三次握手，等待 accept
 acceptChannel_.setReadCallback(
     std::bind(&Acceptor::handleRead, this));
 // 注意：此时还未 listen() / enableReading()，要等 Acceptor::listen() 才注册进 epoll
}

Acceptor::~Acceptor()
{
    // 先从 epoll 移除，再析构 Socket 关闭 listenfd
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();         // 进入 TCP LISTEN 状态
    acceptChannel_.enableReading(); // epoll_ctl ADD listenfd 的 EPOLLIN
}

// listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    // 非阻塞 accept；成功返回 connfd >= 0，peerAddr 填对端地址
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (NewConnectionCallback_)
        {
            // 交给上层（TcpServer::newConnection）：包装 TcpConnection、分发给 subLoop 等
            NewConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            // 未设置回调则关闭，避免泄漏
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        // EMFILE：进程 fd 用尽，常见于未设置 ulimit 或泄漏
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}