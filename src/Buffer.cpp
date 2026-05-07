#include <errno.h>
#include <sys/uio.h>  // readv / iovec
#include <unistd.h>   // write
#include "Buffer.h"

/**
 * 从 fd 读取数据到 Buffer
 *
 * 为什么用 readv？
 * - 单次系统调用可写入多个缓冲区（scatter read）
 * - 先填满 Buffer 自身 writable 区，不够再写入栈上 extrabuf
 * - 若确实超出 writable，再把 extrabuf 追加进 buffer_（可能触发扩容）
 *
 * 这样相比“先试读、再扩容、再读”可减少系统调用次数。
 */
 ssize_t Buffer::readFd(int fd, int *saveErrno)
 {
    // 额外栈缓冲（64KB）
    // 只在一次 readv 读入数据超过 writable 时承接“溢出部分”
    char extrabuf[65536] = {0};

    // iovec 描述一段可写内存
    struct iovec vec[2];//iovec是一个结构体，用于描述一段可写内存,包含起始地址和长度

    // 当前 Buffer 可写大小
    const size_t writable = writableBytes();

    // vec[0] -> Buffer 自身 writable 区
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    // vec[1] -> 栈上临时区
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 若 writable 已很大（>=64KB），只用 vec[0]，避免无意义使用 vec[1]
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;

    // readv 返回读取字节数；<0 表示错误
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0)
    {
        *saveErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        // 全部落在 vec[0]（Buffer writable 区）
        writerIndex_ += static_cast<size_t>(n);
    }
    else
    {
        // vec[0] 填满 + vec[1]（extrabuf）有剩余数据
        writerIndex_ = buffer_.size(); // 先把 vec[0] 的部分视为全部写满
        append(extrabuf, static_cast<size_t>(n) - writable); // 再把溢出追加进 buffer_
    }
    return n;
 }

 /**
 * 把可读区数据写到 fd
 * - 写入起点：peek()（readerIndex_）
 * - 写入长度：readableBytes()
 *
 * 注意：这个函数只负责 write，不自动 retrieve。
 * 上层通常在确认写了 n 字节后，再调用 retrieve(n) 消费输出缓冲。
 */
 ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());//peek()返回的是可读区起始地址，readableBytes()是可读区长度，n是写入的字节数
    if (n < 0)
    {
        *saveErrno = errno;//errno是错误码
    }
    return n;//返回写入的字节数
}