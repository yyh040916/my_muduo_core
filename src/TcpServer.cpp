#include <functional>
#include <string.h>   // memset
#include <sys/socket.h>  // getsockname, sockaddr

#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

/// 构造 TcpServer 时 mainLoop 不能为空
static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop *loop,
                     const InetAddress &listenAddr,
                     const std::string &nameArg,
                     Option option)
    : loop_(CheckLoopNotNull(loop))
    , ipPort_(listenAddr.toIpPort())
    , name_(nameArg)
    , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort))
    , threadPool_(new EventLoopThreadPool(loop, name_))
    , connectionCallback_()
    , messageCallback_()
    , writeCompleteCallback_()
    , threadInitCallback_()
    , numThreads_(0)
    , started_(0)
    , nextConnId_(1)
{
    // listenfd 可读 → Acceptor::handleRead → 调这里的 newConnection
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this,
                  std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    // 服务退出：对每条连接在所属 ioLoop 上 connectDestroyed
    for (auto &item : connections_)
    {
        TcpConnectionPtr conn(item.second);
        item.second.reset();
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    // 与 muduo-core 一致：转给线程池（注意原版此处局部变量遮蔽成员名，成员 numThreads_ 未使用）
    int numThreads_ = numThreads;
    threadPool_->setThreadNum(numThreads_);
}

void TcpServer::start()
{
    // fetch_add：若原值为 0 则本次为第一次 start；否则直接返回（多次 start 无害）
    if (started_.fetch_add(1) == 0)
    {
        threadPool_->start(threadInitCallback_);  // 启动 sub IO 线程（0 个则只调 cb(baseLoop)）

        // listen 必须在 mainLoop 线程执行（Acceptor 挂在 baseLoop 上）
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // ① 轮询选一个 subLoop；numThreads==0 时始终为 baseLoop_
    EventLoop *ioLoop = threadPool_->getNextLoop();

    // ② 生成唯一连接名，例如 "EchoServer-127.0.0.1:8080#1"
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;

    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s\n",
             name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // ③ 本端地址（accept 得到的 connfd 上 getsockname）
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if (::getsockname(sockfd, reinterpret_cast<sockaddr *>(&local), &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);

    // ④ 创建连接对象（必须 shared_ptr，TcpConnection 用了 enable_shared_from_this）
    TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));

    connections_[connName] = conn;

    // ⑤ 把用户设给 TcpServer 的回调，转给 TcpConnection
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // ⑥ 关闭链：TcpConnection::handleClose → closeCallback_ → removeConnection
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // ⑦ 在 ioLoop 线程里：tie、enableReading、connectionCallback(UP)
    ioLoop->runInLoop(
        std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    // close 可能发生在 ioLoop 线程，删 map 必须在 mainLoop 统一做
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
             name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());

    EventLoop *ioLoop = conn->getLoop();
    // 在连接所属 loop 上销毁 Channel 等（queueInLoop 亦可，原版用 queueInLoop）
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
}