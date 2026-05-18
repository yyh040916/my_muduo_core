#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>

#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

/// 构造时检查 loop 非空，避免后面全崩
static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
    const std::string &nameArg,
    int sockfd,
    const InetAddress &localAddr,
    const InetAddress &peerAddr)
: loop_(CheckLoopNotNull(loop))
, name_(nameArg)
, state_(kConnecting)              // 尚未 connectEstablished
, reading_(true)
, socket_(new Socket(sockfd))       // RAII 管理 connfd（注意：Acceptor 已 accept，fd 所有权交给 TcpConnection）
, channel_(new Channel(loop, sockfd))
, localAddr_(localAddr)
, peerAddr_(peerAddr)
, highWaterMark_(64 * 1024 * 1024)  // 64MB 高水位
{
// 把「fd 上发生什么」映射到 TcpConnection 成员函数（都由 Channel::handleEvent 间接调用）
channel_->setReadCallback(
    std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
channel_->setWriteCallback(
    std::bind(&TcpConnection::handleWrite, this));
channel_->setCloseCallback(
    std::bind(&TcpConnection::handleClose, this));
channel_->setErrorCallback(
    std::bind(&TcpConnection::handleError, this));
LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
socket_->setKeepAlive(true);  // TCP keepalive 探测死连接
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n",
             name_.c_str(), channel_->fd(), (int)state_);
}

/// 发送字符串；线程安全：非 IO 线程会 runInLoop 到 IO 线程执行
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            // 已在 IO 线程：直接发
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            // 其他线程：投递到 IO 线程执行 sendInLoop
            loop_->runInLoop(
                std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

/// 必须在 loop 线程执行的真正发送逻辑
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing");
        return;
    }
    // 情况1：当前没在等 EPOLLOUT，且发送缓冲为空 → 尝试直接 write
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 一次写完，不必 enableWriting；写完成回调放到 pending 里执行
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK && errno != EAGAIN)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;  // 对端已关，别再写
                }
            }
        }
    }
    // 情况2：没写完或内核缓冲满 → 剩余数据 append 到 outputBuffer_，并监听可写
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_ &&
            oldLen < highWaterMark_ &&
            highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        outputBuffer_.append(static_cast<const char *>(data) + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting();  // 必须！否则 handleWrite 不会被调
        }
    }
}

void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        // 在loop_线程执行shutdownInLoop
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

/// 在loop_线程执行shutdownInLoop
void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 说明当前outputBuffer_的数据全部向外发送完成
    {
        socket_->shutdownWrite();
    }
}

/// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());  // 防止 TcpConnection 已析构仍执行回调
    channel_->enableReading();           // 注册 EPOLLIN，开始收数据

    if (connectionCallback_)
    {
        connectionCallback_(shared_from_this());  // 用户：连接建立（UP/DOWN 语义由用户区分）
    }
}

/// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();
        if (connectionCallback_)
        {
            connectionCallback_(shared_from_this());  // 用户：连接销毁通知
        }
    }
    channel_->remove();
}

/// Channel 读回调：从 socket 读到 inputBuffer_，再 messageCallback_
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        // 有数据：交给用户；用户通常 buf->retrieve... 消费 inputBuffer_
        if (messageCallback_)
        {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        }
    }
    else if (n == 0)
    {
        // 对端关闭写端，EOF
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

/// outputBuffer_ 有数据且 EPOLLOUT：继续 write
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();  // 发完了，不必再关心 EPOLLOUT
                if (writeCompleteCallback_)
                {
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();  // 之前在 shutdown，现在缓冲发完可以关写端
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}

/// 对端关闭或本地关闭路径
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();
    TcpConnectionPtr connPtr(shared_from_this());
    if (connectionCallback_)
    {
        connectionCallback_(connPtr);
    }
    if (closeCallback_)
    {
        closeCallback_(connPtr);  // TcpServer::removeConnection 通常绑在这里
    }
}

/// 读/写错误
void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)//获取socket的错误信息
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}

/// 零拷贝发送文件（Linux sendfile）；学习阶段可先实现，测试可不用
void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count)
{
    if (connected())
    {
        if (loop_->isInLoopThread())
        {
            sendFileInLoop(fileDescriptor, offset, count);
        }
        else
        {
            loop_->runInLoop(
                std::bind(&TcpConnection::sendFileInLoop, shared_from_this(),
                          fileDescriptor, offset, count));
        }
    }
    else
    {
        LOG_ERROR("TcpConnection::sendFile - not connected");
    }
}

/// 在loop_线程执行sendFileInLoop
void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count)
{
    ssize_t bytesSent = 0;
    size_t remaining = count;
    bool faultError = false;
    if (state_ == kDisconnecting)
    {
        LOG_ERROR("disconnected, give up writing");
        return;
    }
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        bytesSent = ::sendfile(socket_->fd(), fileDescriptor, &offset, remaining);
        if (bytesSent >= 0)
        {
            remaining -= bytesSent;
            if (remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendFileInLoop");
            }
            if (errno == EPIPE || errno == ECONNRESET)
            {
                faultError = true;
            }
        }
    }
    if (!faultError && remaining > 0)
    {
        // 原版：剩余继续排队 sendFileInLoop（简化零拷贝路径）
        loop_->queueInLoop(
            std::bind(&TcpConnection::sendFileInLoop, shared_from_this(),
                      fileDescriptor, offset, remaining));
    }
}