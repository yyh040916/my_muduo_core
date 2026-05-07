#include "InetAddress.h"
#include <iostream>

int main()
{
    InetAddress a(8080, "0.0.0.0");
    std::cout << a.toIpPort() << std::endl; // 期望类似 0.0.0.0:8080

    InetAddress b(9090);
    std::cout << b.toIpPort() << std::endl; // 默认 IP 127.0.0.1

    std::cout << "port host order = " << b.toPort() << std::endl;
    return 0;
}