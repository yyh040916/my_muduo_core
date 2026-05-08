#pragma once

#include "noncopyable.h"

// 前向声明：头文件里只有指针/引用，不必包含 InetAddress.h，减轻编译依赖
class InetAddress;

/**
 * Socket：封装「已存在的」套接字 fd（不负责 socket(AF_INET, SOCK_STREAM, 0) 的那一步，
 * 一般由 TcpServer/上层先创建再交给 Socket）
 *
 * noncopyable：禁止拷贝/赋值，避免两个 Socket 对象关同一个 fd 或 double-close。
 */
class Socket : noncopyable
{
public:
    /**
     * explicit：禁止 int 隐式转成 Socket。
     * sockfd：必须是合法的文件描述符（监听 fd 或连接 fd）。
     */
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {
    }
    
    /// 析构里 close(sockfd_)，离开作用域自动释放内核资源（RAII）
    ~Socket();

    /// 取出底层 fd，交给 epoll、read/write 等
    int fd() const { return sockfd_; }

    /// 绑定本地地址（服务端 listen 前调用）
    void bindAddress(const InetAddress &localaddr);

    /// 开始监听（bind 之后调用）
    void listen();

    /**
     * 接受新连接。
     * peeraddr：若非空，填充对端的 sockaddr_in（accept 返回时内核写入的对端地址）。
     * 返回值：新连接的 connfd；失败时返回负数（需结合 errno，原版里 LOG_FATAL 在别处）。
     */
     int accept(InetAddress *peeraddr);

    /// 关闭写半部（发送 FIN），读仍可继续一段时间
    void shutdownWrite();

    /// setsockopt 封装：是否禁用 Nagle（小延迟发送），Nagle算法是一种网络协议优化算法，用于减少网络传输中的小数据包数量，从而提高网络传输效率。
    void setTcpNoDelay(bool on);

    /// 允许地址重用（服务端快速重启常用）
    void setReuseAddr(bool on);

    /// 允许多个 socket 绑定同一端口（内核负载均衡，Linux）
    void setReusePort(bool on);

    /// TCP keepalive：检测对端是否还活着
    void setKeepAlive(bool on);

private:
    /// fd 一旦交给 Socket，生命周期由本对象管理；const 表示构造后 fd 不变（muduo 教学版）
    const int sockfd_;
};