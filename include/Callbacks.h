#pragma once
// 头文件防重包含：同一个翻译单元里多次 #include 本文件也只会展开一次。
#include <memory>      // std::shared_ptr
#include <functional>  // std::function

// ========================== 前向声明（Forward Declaration） ==========================
// 这里只是“告诉编译器这些名字是类”，不需要类的完整定义。
// 好处：
// 1) 减少头文件依赖，编译更快
// 2) 避免头文件互相 include 形成循环依赖
//
// 什么时候必须改成 #include "xxx.h"？
// - 当你在本头文件里需要“对象的具体大小”或“访问其成员”时。
// - 这里只在类型里用指针/引用/shared_ptr，所以前向声明足够。
class Buffer;
class TcpConnection;
class Timestamp;

// using 类型别名：给长类型起简短名字，提高可读性。
// 旧写法是 typedef，这里用 C++11 的 using，语法更直观。

// TcpConnectionPtr：连接对象的共享智能指针。
// 为什么 shared_ptr 而不是裸指针？
// - 连接对象会被多个地方共同持有（连接表、回调、事件循环等）
// - shared_ptr 用引用计数管理生命周期，最后一个持有者释放时自动 delete
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// ConnectionCallback：连接建立/断开时回调类型。
// std::function<返回值(参数列表)> 解释：
// - 返回值：void（不返回）
// - 参数：const TcpConnectionPtr&（只读引用，避免拷贝 shared_ptr 引用计数）
//
// 对应可赋值内容：
// - 普通函数
// - lambda
// - std::bind 绑定后的可调用对象
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;

// CloseCallback：连接关闭时回调，签名和 ConnectionCallback 一样。
// 分成不同别名的意义：
// - 类型层面虽然相同，但语义不同（读代码更清晰）
// - 看到 setCloseCallback() 就知道用途
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

// WriteCompleteCallback：数据发送完成时回调。
// 同样签名，只是语义是“写完成通知”。
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;

// HighWaterMarkCallback：高水位回调（输出缓冲区太大时触发）。
// 第二个参数 size_t 通常表示：当前输出缓冲区字节数（或达到阈值时的长度）。
// 用它做“背压控制”：例如暂停继续写，防止内存无限增长。
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr &, size_t)>;

// MessageCallback：收到消息时回调（最核心）。
// 三个参数分别是：
// 1) const TcpConnectionPtr& conn：哪条连接收到数据
// 2) Buffer*                  buf ：收到的数据缓冲区（通常从中 retrieve 读取）
// 3) Timestamp                time：收到/处理该批数据时的时间戳
//
// 注意：Timestamp 在这里按值传递（你的原版就是这样写的）。
// 如果后续你希望减少拷贝，也可改成 const Timestamp&，但先保持和原版一致更好。
using MessageCallback = std::function<void(const TcpConnectionPtr &,
    Buffer *,
    Timestamp)>;