#pragma once

#include <string>

#include "noncopyable.h"

// 宏 LOG_INFO / LOG_ERROR / LOG_FATAL / LOG_DEBUG 在「预处理阶段」做文本替换，
/**
 * LOG_INFO(格式串, 可变参数...)
 *
 * 展开后做的事（按顺序）：
 *   1) Logger::instance() 取全进程唯一的 Logger 引用
 *   2) setLogLevel(INFO) 记下「本条日志用 INFO 前缀」
 *   3) snprintf 把格式串和参数写进栈数组 buf（防溢出，最多 1024 字节含 '\0'）
 *   4) logger.log(buf) 真正打印：前缀 + 时间 + 消息
 */
 #define LOG_INFO(logmsgFormat, ...)                       \
  /* do { ... } while (0) 外壳：多语句宏写成一条「语法上的语句」，且避免 if/else 与分号坑 */ \
  do                                                    \
  {                                                     \
    /* 引用：必须绑定已有对象，不能空；这里绑定静态单例 */              \
    Logger &logger = Logger::instance();              \
    /* 设置：全局日志级别，决定是否打印本条 */              \
    logger.setLogLevel(INFO);                         \
    /* 准备：栈数组 buf 预留 1024 字节，最后加 '\0' */              \
    char buf[1024] = {0};                             \
    /* 写入：格式串 + 可变参数，写入 buf */              \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__); \
    /* 打印：调用底层日志方法 */              \
    logger.log(buf);                                  \
  } while (0) /* 条件恒假，循环体只执行一次；末尾分号由调用处 LOG_INFO(...); 提供 */

#define LOG_ERROR(logmsgFormat, ...)                      \
  do                                                    \
  {                                                     \
    Logger &logger = Logger::instance();              \
    logger.setLogLevel(ERROR);                         \
    char buf[1024] = {0};                             \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__); \
    logger.log(buf);                                  \
  } while (0)

  
#define LOG_FATAL(logmsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(FATAL);                        \
        char buf[1024] = {0};                             \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__); \
        logger.log(buf);                                  \
        /* 非零退出码：告诉 shell/父进程异常结束；此后 main 里代码不会执行 */ \
        exit(-1);                                         \
    } while (0)

// 编译时若定义了宏 MUDEBUG（例如 g++ -DMUDEBUG 或 CMake target_compile_definitions），
// LOG_DEBUG 展开成带代码的版本；否则展开成「空」，调试字符串不进二进制、不占运行时间。
#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::instance();              \
        logger.setLogLevel(DEBUG);                        \
        char buf[1024] = {0};                             \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__); \
        logger.log(buf);                                  \
    } while (0)
#else
// 宏体为空：调用处写 LOG_DEBUG(...); 预处理后整句消失，只剩一个可有可无的分号场景由编译器容忍
#define LOG_DEBUG(logmsgFormat, ...)
#endif

// 枚举成员底层一般是 int，默认从 0 递增：INFO=0, ERROR=1, FATAL=2, DEBUG=3
enum LogLevel
{
    INFO,
    ERROR,
    FATAL,
    DEBUG,
};

/**
 * Logger：教学版同步日志（cout），单例。
 * private 继承 noncopyable：禁止拷贝/赋值 Logger，避免出现两个全局日志对象。
 */
class Logger : noncopyable
{
public:
    // 获取日志唯一的实例对象 单例
    static Logger &instance();
    // 设置日志级别
    void setLogLevel(int level);
    // 写日志
    void log(std::string msg);

private:
    int logLevel_;
};