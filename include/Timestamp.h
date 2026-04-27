#pragma once
#include <iostream>
#include <string>

/**
 * Timestamp：表示「某一时刻」。
 * 在 muduo 风格网络库中的典型用途：
 *   - Poller::poll 返回「本轮 epoll_wait 返回时」的时间点；
 *   - Channel::handleEvent(Timestamp)、MessageCallback(..., Timestamp) 把时刻传给业务。
 *
 * 【阅读注意】本教学仓库实现中：
 *   - now() 使用 time(NULL)，得到的是自 1970-01-01 起的「秒」（time_t 语义）；
 *   - 该值存入成员 microSecondsSinceEpoch_，名字含 micro，但实际与 now() 搭配时是「秒」；
 *   - toString() 里把 microSecondsSinceEpoch_ 当作秒交给 localtime 解析。
 * 与正式 muduo 书中「微秒时间戳」命名一致、实现细节以本文件为准。
 */
class Timestamp
{
public:
    /// 默认构造：内部计数置 0
    Timestamp();
    /// 显式单参构造：禁止 int64_t 隐式转成 Timestamp；参数在接口命名上表示「纪元起的微秒」，
    /// 在本项目中若与 now() 一致使用，传入的应是可与 time(NULL) 同语义的秒级整型。
    explicit Timestamp(int64_t microSecondsSinceEpoch);

    /// 获取当前时刻（自 1970-01-01 起的秒）
    static Timestamp now(); 
    /// 将内部计数转换为字符串格式
    std::string toString() const;

private:
    /// 内部存储：自 Unix 纪元起的计数；与本文件 now()/toString 实现连用时为「秒」
    int64_t microSecondsSinceEpoch_;
};