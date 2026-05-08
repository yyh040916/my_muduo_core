#include <unistd.h>      // close
#include <sys/types.h>
#include <sys/socket.h>  // bind listen accept4 shutdown setsockopt
#include <string.h>      // memset
#include <netinet/tcp.h> // TCP_NODELAY

// 若编译器报 accept4 未声明，可确认 _GNU_SOURCE（glibc）；Linux 下一般已有
#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

Socket::~Socket()
{
    // 关闭文件描述符：内核回收与该 fd 相关的资源；重复 close 未定义行为，故禁止拷贝 Socket
    ::close(sockfd_);
}

void Socket::bindAddress(const InetAddress &localaddr)
{
    // bind 需要 sockaddr*；IPv4 下 sockaddr_in* 可强转为 sockaddr*（经典写法）
    // sizeof(sockaddr_in)：告诉内核地址结构长度
    //bind: 将套接字绑定到指定的地址和端口
    if (0 != ::bind(sockfd_, (sockaddr *)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        LOG_FATAL("bind sockfd:%d fail\n", sockfd_);
    }
}

void Socket::listen()
{
    // listen: 开始监听连接请求
    if (0 != ::listen(sockfd_, 1024))//1024: 内核允许的最大未处理连接数
    {
        LOG_FATAL("listen sockfd:%d fail\n", sockfd_);
    }
}

int Socket::accept(InetAddress *peeraddr)
{
    /*
     * accept4（Linux）：一次调用完成 accept + 设置标志
     * - SOCK_NONBLOCK：连接 socket 非阻塞，配合 epoll 边缘/水平触发
     * - SOCK_CLOEXEC：exec 其它程序时自动关闭 fd，防止泄漏到子进程
     *
     * 若 peeraddr 非空，把内核返回的对端地址写回 InetAddress
     */
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    ::memset(&addr, 0, sizeof(addr));
    //fixed : int connfd = ::accept(sockfd_, (sockaddr *)&addr, &len);
    //accept4: 一次调用完成 accept + 设置标志
    //- SOCK_NONBLOCK：连接 socket 非阻塞，配合 epoll 边缘/水平触发
    //- SOCK_CLOEXEC：exec 其它程序时自动关闭 fd，防止泄漏到子进程
    int connfd = ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0 && peeraddr != nullptr)//peeraddr不为空，则将内核返回的对端地址写回InetAddress
    {
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}


void Socket::shutdownWrite()
{
    // SHUT_WR：关闭写方向；对端 read 可能读到 0（EOF）
    //shutdown: 关闭套接字，SHUT_WR: 关闭写方向
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdownWrite error");
    }
}

void Socket::setTcpNoDelay(bool on)
{
    // TCP_NODELAY=1：关闭 Nagle，小数据包尽快发出（延迟换吞吐）
    int optval = on ? 1 : 0;
    //setsockopt: 设置套字选项，IPPROTO_TCP: 指定使用TCP协议，TCP_NODELAY: 关闭Nagle算法
    //&optval: 选项值
    //static_cast<socklen_t>(sizeof(optval)): 选项值长度
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval,
                 static_cast<socklen_t>(sizeof(optval)));
}

void Socket::setReuseAddr(bool on)
{
    // TIME_WAIT 等场景下允许绑定处于可用状态的地址，便于服务端重启
    int optval = on ? 1 : 0;
    //setsockopt: 设置套接字选项，SOL_SOCKET: 指定使用socket层，SO_REUSEADDR: 允许地址重用
    //&optval: 选项值
    //static_cast<socklen_t>(sizeof(optval)): 选项值长度
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval,
                 static_cast<socklen_t>(sizeof(optval)));
}

void Socket::setReusePort(bool on)
{
    // 多进程/多线程监听同一端口，由内核分发连接（需 Linux 较新版本支持良好）
    int optval = on ? 1 : 0;
    //setsockopt: 设置套接字选项，SOL_SOCKET: 指定使用socket层，SO_REUSEPORT: 允许端口重用
    //&optval: 选项值
    //static_cast<socklen_t>(sizeof(optval)): 选项值长度
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval,
                 static_cast<socklen_t>(sizeof(optval)));
}

void Socket::setKeepAlive(bool on)
{
    // 定期探测连接是否仍存活（细节由内核参数控制）
    int optval = on ? 1 : 0;
    //setsockopt: 设置套接字选项，SOL_SOCKET: 指定使用socket层，SO_KEEPALIVE: 定期探测连接是否仍存活
    //&optval: 选项值
    //static_cast<socklen_t>(sizeof(optval)): 选项值长度
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval,
                 static_cast<socklen_t>(sizeof(optval)));
}
