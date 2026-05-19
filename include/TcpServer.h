#pragma once

/**
 * TcpServer：muduo 对外的 TCP 服务器类。
 *
 * 用户典型写法：
 *   EventLoop loop;                    // mainLoop
 *   TcpServer server(&loop, addr, "Echo");
 *   server.setThreadNum(4);
 *   server.setMessageCallback(...);
 *   server.start();
 *   loop.loop();
 */

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"          // Acceptor 等用；TcpServer 本身可拷贝（muduo 未禁拷贝）
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

class TcpServer
{
public:
    /// 每个 sub IO 线程 EventLoop 创建后、loop() 前的钩子（转给线程池）
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    enum Option
    {
        kNoReusePort,   /// Acceptor 构造时 reuseport 参数为 false（仍可能设 REUSEADDR，见 Acceptor 实现）
        kReusePort,     /// 允许多进程/多实例 SO_REUSEPORT（若系统支持）
    };

    /**
     * @param loop       main 线程的 EventLoop（baseLoop），负责 accept
     * @param listenAddr 监听地址
     * @param nameArg    服务名，用于日志和连接名前缀
     * @param option     是否 reuse port
     */
    TcpServer(EventLoop *loop,
              const InetAddress &listenAddr,
              const std::string &nameArg,
              Option option = kNoReusePort);
    ~TcpServer();

    void setThreadInitCallback(const ThreadInitCallback &cb)
    { threadInitCallback_ = cb; }

    /// 连接建立/断开时（TcpConnection 里 connectionCallback_ 会调）
    void setConnectionCallback(const ConnectionCallback &cb)
    { connectionCallback_ = cb; }

    /// 收到数据时
    void setMessageCallback(const MessageCallback &cb)
    { messageCallback_ = cb; }

    /// 发送缓冲区写完时
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    { writeCompleteCallback_ = cb; }

    /// 设置 sub IO 线程个数；须在 start() 之前调用
    void setThreadNum(int numThreads);

    /**
     * 启动：只执行一次（started_ 原子防重入）
     * 1) threadPool_->start
     * 2) 在 main loop 里 Acceptor::listen
     */
    void start();

private:
    /// Acceptor 新连接回调：创建 TcpConnection，放进 map，connectEstablished
    void newConnection(int sockfd, const InetAddress &peerAddr);

    /// TcpConnection 关闭时回调：转到 main loop 删连接
    void removeConnection(const TcpConnectionPtr &conn);

    /// 在 main loop 线程执行：从 map 删除 + 在 ioLoop 上 connectDestroyed
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_;              /// baseLoop / mainLoop，accept 在此
    const std::string ipPort_;     /// 监听点 "ip:port" 字符串，拼连接名用
    const std::string name_;       /// 服务器名

    std::unique_ptr<Acceptor> acceptor_;                  /// 只在 mainLoop 上跑
    std::shared_ptr<EventLoopThreadPool> threadPool_;     /// IO 线程池

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    ThreadInitCallback threadInitCallback_;

    int numThreads_;               /// muduo 头文件有；实现里主要用 threadPool_ 设（与原版一致）
    std::atomic_int started_;    /// 0→1 表示已 start，防多次 start
    int nextConnId_;             /// 连接序号，只在 mainLoop 的 newConnection 里 ++
    ConnectionMap connections_;  /// 所有活跃连接
};