#include "noncopyable.h"

#include <cstdio>

// 模拟一个「持有资源、不该被拷贝」的类型
class ResourceHolder : private noncopyable
{
public:
    ResourceHolder() { std::puts("ResourceHolder 构造"); }
    ~ResourceHolder() { std::puts("ResourceHolder 析构"); }

    void use() const { std::puts("use() 正常调用"); }
};

int main()
{
    ResourceHolder a; // OK：派生类可正常构造
    a.use();

    ResourceHolder b; // OK：多个独立对象
    b.use();

    // 下面两行若去掉注释，应「编译失败」——验证 noncopyable 生效
    // ResourceHolder c = a;            // 错误：拷贝构造被 delete
    // ResourceHolder d; d = a;         // 错误：拷贝赋值被 delete

    return 0;
}