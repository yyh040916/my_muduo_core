#pragma once
// arpa/inet.h：inet_addr / inet_ntop 等
#include <arpa/inet.h>
//netinet/in.h：sockaddr_in / AF_INET / INADDR_ANY 等
#include <netinet/in.h>
#include <string>

/**
 * InetAddress：封装 IPv4 的 sockaddr_in，供 bind/listen/connect/accept 使用。
 *
 * 为什么不直接用 sockaddr_in？
 * - C 结构体散落各处，端口字节序、IP 字符串转换容易写错
 * - 类封装后构造、打印、传递更清晰，与 Socket / TcpConnection 更好协作
 */
class InetAddress
{
public:
    /**
     * 用「端口 + IP 字符串」构造。
     * explicit：禁止 uint16_t 被隐式转换成 InetAddress，避免意外构造。
     *
     * port：主机字节序的端口号（例如 8080），内部会 htons 转成网络字节序。
     * ip：点分十进制 IPv4，默认 "127.0.0.1"；也可用 "0.0.0.0" 表示任意地址。
     */
    explicit InetAddress(uint16_t port=0, std::string ip="127.0.0.1");

    /**
     * 从已有的 sockaddr_in 拷贝构造（例如 accept 填充的对端地址）。
     * 这里用初始化列表直接拷贝 addr_，函数体为空即可。
     */
     explicit InetAddress(const sockaddr_in &addr)
     : addr_(addr)
    {
    }
    /// 只返回 IP 字符串，例如 "192.168.1.1"
    std::string toIp() const;
    /// 返回 "ip:port"，例如 "192.168.1.1:8080"
    std::string toIpPort() const;
    /// 返回主机字节序的端口号（内部 sin_port 会先 ntohs）
    uint16_t toPort() const;
    /// 交给 bind/connect 等：需要 sockaddr_in 指针时用这个
    const sockaddr_in *getSockAddr() const { return &addr_; }
    /// 少数场景需要整体替换地址（例如从别处拷贝过来）
    void setSockAddr(const sockaddr_in &addr) { addr_ = addr; }
private:
    sockaddr_in addr_; // IPv4 套接字地址（含 family、端口、IP）
};