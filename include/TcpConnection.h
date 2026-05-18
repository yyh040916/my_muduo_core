#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"   // TcpConnectionPtr、MessageCallback 等
#include "Buffer.h"
#include "Timestamp.h"

class Channel;
class EventLoop;
class Socket;

/**
 * TcpConnection：表示一条 TCP 连接（已 accept 的 connfd）。
 *
 * 继承 enable_shared_from_this：
 *   - 回调里需要 TcpConnectionPtr，且 channel_->tie(shared_from_this()) 延长生命周期。
 *
 * 典型创建（TcpServer::newConnection，你 main 里会仿造）：
 *   TcpConnectionPtr conn(new TcpConnection(loop, name, connfd, local, peer));
 *   conn->setMessageCallback(...);
 *   loop->runInLoop(bind(&TcpConnection::connectEstablished, conn));
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
     TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress &localAddr,
                const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }

    /// 是否处于已连接且可收发状态
    bool connected() const { return state_ == kConnected; }

    /// 发送字符串；线程安全：非 IO 线程会 runInLoop 到 IO 线程执行
    void send(const std::string &buf);

    /// 零拷贝发送文件（Linux sendfile）；学习阶段可先实现，测试可不用
    void sendFile(int fileDescriptor, off_t offset, size_t count);

    /// 半关闭写端（FIN），常用于优雅关闭
    void shutdown();

    // ----- 用户回调：由 TcpServer 转传，TcpConnection 在适当时机调用 -----
    void setConnectionCallback(const ConnectionCallback &cb)
    { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb)
    { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback &cb)
    { closeCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

    /**
     * 连接建立完成：在 IO 线程调用。
     * tie + enableReading + 调 connectionCallback_(UP)。
     */
     void connectEstablished();

     /**
      * 连接销毁收尾：disableAll、connection 回调、从 Poller remove Channel。
      */
     void connectDestroyed();

private:
    enum StateE
    {
        kDisconnected, // 已经断开连接
        kConnecting,   // 正在连接
        kConnected,    // 已连接
        kDisconnecting // 正在断开连接
    };
    void setState(StateE state) { state_ = state; }
    
    /// Channel 读回调：从 socket 读到 inputBuffer_，再 messageCallback_
    void handleRead(Timestamp receiveTime);
    /// outputBuffer_ 有数据且 EPOLLOUT：继续 write
    void handleWrite();
    /// 对端关闭或本地关闭路径
    void handleClose();
    /// 读/写错误
    void handleError();

    /// 必须在 loop 线程执行的真正发送逻辑
    void sendInLoop(const void *data, size_t len);
    void shutdownInLoop();
    void sendFileInLoop(int fileDescriptor, off_t offset, size_t count);

    EventLoop *loop_;              /// 所属 IO 线程的 EventLoop
    const std::string name_;       /// 连接名，日志用
    std::atomic_int state_;
    bool reading_;                 /// 是否监听读（原版成员，逻辑保留）

    // Socket Channel 这里和Acceptor类似    Acceptor => mainloop    TcpConnection => subloop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    // 本地地址和远程地址
    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    // 这些回调TcpServer也有 用户通过写入TcpServer注册 TcpServer再将注册的回调传递给TcpConnection TcpConnection再将回调注册到Channel中
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
    size_t highWaterMark_;         /// 发送缓冲超过阈值时回调（默认 64MB）
    Buffer inputBuffer_;           /// 收到的数据
    Buffer outputBuffer_;          /// 待发送的数据
};