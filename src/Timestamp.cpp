#include <time.h>
#include "Timestamp.h"

// 默认构造：microSecondsSinceEpoch_ 为 0
Timestamp::Timestamp() : microSecondsSinceEpoch_(0)
{
}

// 用调用方传入的 int64 初始化内部计数（不做范围校验）
Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch)
{
}

// 当前时刻：time(NULL) 返回从 Epoch 到现在的秒数（time_t），
// 用于构造 Timestamp，写入 microSecondsSinceEpoch_。
Timestamp Timestamp::now()
{
    return Timestamp(time(NULL));
}

std::string Timestamp::toString() const
{
    char buf[128] = {0};
    // localtime 参数为「指向秒级 time_t」的指针；此处 microSecondsSinceEpoch_
    // 在经由 now() 赋值的路径下与秒级 time_t 一致，故可分解为年月日时分秒。
    // 注意：localtime 返回静态分配的 tm*，多线程同时调用可能互相覆盖（非线程安全）。
    tm *tm_time = localtime(&microSecondsSinceEpoch_);
    snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d",
             tm_time->tm_year + 1900,
             tm_time->tm_mon + 1,
             tm_time->tm_mday,
             tm_time->tm_hour,
             tm_time->tm_min,
             tm_time->tm_sec);
    return buf;
}