#include <iostream>
#include "Logger.h"
#include "Timestamp.h"  // log() 里要打时间戳

Logger &Logger::instance()
{
    // 函数局部静态变量：整个程序只有这一份 Logger 存储
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(int level)
{
    // 把 int 存起来；宏里传入的是 INFO/ERROR 等枚举值，会提升为 int
    logLevel_ = level;
}

void Logger::log(std::string msg)
{
    // 先根据 logLevel_ 选人类可读前缀
    std::string pre = "";
    switch (logLevel_)
    {
    case INFO:
        pre = "[INFO]";
        break;
    case ERROR:
        pre = "[ERROR]";
        break;
    case FATAL:
        pre = "[FATAL]";
        break;
    case DEBUG:
        pre = "[DEBUG]";
        break;
    default:
        // 未匹配则 pre 仍是 ""，输出里就没有方括号标签
        break;
    }
    // pre 是 string，+ 右侧 Timestamp::now().toString() 也返回 string（或隐式转换链），
    // 得到「前缀+时间字符串」；再 << " : " << msg 拼上正文与换行
    std::cout << pre + Timestamp::now().toString() << " : " << msg << std::endl;
}