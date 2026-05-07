#include <strings.h> // bzero 某些代码会用；本文件主要用 memset
#include <string.h>  // memset, strlen
#include <stdio.h>   // sprintf（原版 toIpPort 使用；生产环境可换 snprintf）
#include "InetAddress.h"


InetAddress::InetAddress(uint16_t port, std::string ip)
{
    // 整个结构体清零，避免未初始化字段带来未定义行为
    ::memset(&addr_, 0, sizeof(addr_));//addr_: 套接字地址

    addr_.sin_family = AF_INET; // IPv4,AF_INET: IPv4 协议族,sin_family: 地址族

    // htons：host to network short，把本机端口号转成网络字节序（大端）
    addr_.sin_port = ::htons(port);//sin_port: 端口号

    // inet_addr：把 "127.0.0.1" 这类字符串转成 network byte order 的 s_addr
    // 注意：inet_addr 失败时返回 INADDR_NONE；严谨场景可用 inet_pton 替代
    addr_.sin_addr.s_addr = ::inet_addr(ip.c_str());//inet_addr: 把 "127.0.0.1" 这类字符串转成网络字节序的 s_addr
}

std::string InetAddress::toIp() const
{
    // addr_
    char buf[64] = {0};

    // inet_ntop：二进制地址 -> 字符串，线程安全（优于过时的 inet_ntoa）
    // AF_INET：IPv4；&addr_.sin_addr 指向 in_addr；buf 为输出缓冲；sizeof buf 防溢出
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    return buf;
}

std::string InetAddress::toIpPort() const
{
    char buf[64] = {0};
    // 先把 IP 放进 buf
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    size_t end = ::strlen(buf);

    // ntohs：network to host short，把端口从网络字节序转回本机 uint16_t
    uint16_t port = ::ntohs(addr_.sin_port);
    // 在 IP 字符串后面拼接 ":port"；原版用 sprintf，buf 足够大时可行
    ::sprintf(buf + end, ":%u", static_cast<unsigned>(port));//sprintf: 格式化字符串
    return buf;
}

uint16_t InetAddress::toPort() const
{
    return ::ntohs(addr_.sin_port);//ntohs: 网络字节序转成本机字节序
}
