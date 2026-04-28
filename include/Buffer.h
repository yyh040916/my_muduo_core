#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <stddef.h>  // size_t

/**
 * Buffer：网络库底层缓冲区（面向 TCP 字节流）
 *
 * 内部模型（非常关键）：
 *   [0, readerIndex_)             -> prependable（可预留/已读区域）
 *   [readerIndex_, writerIndex_)  -> readable（可读区域：业务还没取走的数据）
 *   [writerIndex_, buffer_.size()) -> writable（可写区域：可继续 append）
 *
 * 为什么要这样分区？
 *   - 避免每次读取后都 memmove 全部数据
 *   - 可以通过移动 readerIndex_/writerIndex_ 管理“逻辑上的读写”
 *   - 必要时再扩容或搬移有效数据（makeSpace）
 */
class Buffer
{
public:
    // 头部预留 8 字节（muduo 经典值）
    // 常用于协议编码时在包头前面 prepend 长度字段等。
    static const size_t kCheapPrepend = 8;
    // 初始可用正文空间 1024 字节
    static const size_t kInitialSize = 1024;

    /**
     * 构造：
     * - vector 大小 = 预留区 + 初始正文区
     * - readerIndex_ / writerIndex_ 都从 kCheapPrepend 起步
     *   => 初始 readable=0, writable=kInitialSize
     */
    explicit Buffer(size_t initalSize = kInitialSize)
     : buffer_(kCheapPrepend + initalSize)
     , readerIndex_(kCheapPrepend)
     , writerIndex_(kCheapPrepend)
    {
    }

    /// 可读字节数 = writer - reader
    size_t readableBytes() const { return writerIndex_ - readerIndex_; }

    /// 可写字节数 = 总容量 - writer
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }

    /// 头部可预留字节数 = reader（reader 前面都可视为 prepend 区）
    size_t prependableBytes() const { return readerIndex_; }

    /// 返回可读区起始地址（只读）
    const char *peek() const { return begin() + readerIndex_; }

    /**
     * 消费 len 字节
     * - 若 len < readable：只移动 readerIndex_
     * - 若 len >= readable：等价于全部消费，重置到初始位置
     */
     void retrieve(size_t len)
     {
         if (len < readableBytes())
         {
             readerIndex_ += len;
         }
         else
         {
             retrieveAll();
         }
     }

     /// 全部消费：把读写指针都重置到预留区起点（不缩容，不清数据）
    void retrieveAll()
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    /// 把全部可读区转为 string 并消费
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }

    /**
     * 取 len 字节字符串并消费
     * 注意：调用方应保证 len <= readableBytes()（这里按原版不做额外防御）
     */
     std::string retrieveAsString(size_t len)
     {
         std::string result(peek(), len);//将可读区的前len个字节转换为string类型
         retrieve(len);//将可读区的前len个字节消费掉
         return result;
     }

     /**
     * 确保至少有 len 字节可写空间
     * 不够则 makeSpace(len)：要么扩容，要么搬移现有可读数据腾空间
     */
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
    }

    /**
     * 追加 [data, data+len) 到可写区
     * 步骤：
     *  1) ensureWritableBytes(len)
     *  2) copy 到 beginWrite()
     *  3) writerIndex_ 前移 len
     */
     void append(const char *data, size_t len)
     {
         ensureWritableBytes(len);
         std::copy(data, data + len, beginWrite());
         writerIndex_ += len;
     }

     /// 可写区起始地址（可写）
    char *beginWrite() { return begin() + writerIndex_; }
    /// 可写区起始地址（只读版本）
    const char *beginWrite() const { return begin() + writerIndex_; }
    /// 从 fd 读数据到 Buffer（实现见 Buffer.cc）
    ssize_t readFd(int fd, int *saveErrno);
    /// 把 Buffer 的可读数据写到 fd（实现见 Buffer.cc）
    ssize_t writeFd(int fd, int *saveErrno);

private:
    /// vector 起始地址
    char *begin() { return &*buffer_.begin(); }
    const char *begin() const { return &*buffer_.begin(); }

    /**
     * 扩空间策略（核心）：
     *
     * 当前布局示意：
     * [prependable(含已读垃圾)] [readable有效数据] [writable空闲]
     *
     * 如果「writable + prependable」仍不足以容纳 len + kCheapPrepend：
     *   -> 直接 resize 扩容
     *
     * 否则：
     *   -> 把 readable 数据搬到 kCheapPrepend 后面（压缩前面垃圾）
     *   -> 更新 readerIndex_/writerIndex_
     *
     * 这样既减少扩容次数，又尽量复用已分配内存。
     */
     void makeSpace(size_t len)
     {
         if (writableBytes() + prependableBytes() < len + kCheapPrepend)
         {
             // 不够：扩大容量到至少 writer+len
             buffer_.resize(writerIndex_ + len);
         }
         else
         {
             // 够：搬移 readable 数据到前部（紧贴预留区）
             size_t readable = readableBytes();
             std::copy(begin() + readerIndex_,
                       begin() + writerIndex_,
                       begin() + kCheapPrepend);
             readerIndex_ = kCheapPrepend;
             writerIndex_ = readerIndex_ + readable;
         }
     }

private:
    std::vector<char> buffer_; // 底层连续存储
    size_t readerIndex_;       // 可读起点
    size_t writerIndex_;       // 可写起点（也即可读终点）
};