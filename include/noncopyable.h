#pragma once // 防止头文件重复包含


/**
 * 空基类：没有非静态数据成员，一般不占派生对象额外空间（空基类优化 EBO）。
 * 用途：被「持有资源、禁止拷贝」的类型继承，例如 EventLoop、Channel、Socket。
 *
 * 为什么网络库里常见「禁止拷贝」？
 * - 拷贝会复制 fd / 指针，容易出现「两个对象关同一个 fd」或「重复 epoll_ctl」。
 * - 把拷贝构造、拷贝赋值标为 = delete，在编译期就拒绝错误用法。
 */
class noncopyable
{
public:
    // 禁止拷贝构造：派生类也不可被拷贝构造（继承后规则一并适用）
    noncopyable(const noncopyable &) = delete;
    // 禁止拷贝赋值
    noncopyable &operator=(const noncopyable &) = delete;
protected:
    // 允许默认构造：派生类可以正常构造（但禁止拷贝）
    noncopyable() = default;
    // 允许默认析构：派生类可以正常析构
    ~noncopyable() = default;
};
