#ifndef SHARED_DATA_POOL_H
#define SHARED_DATA_POOL_H

#include "Common.h"
#include "ProcessRWLock.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace IPC {

// ========== 共享内存头部结构 ==========

/**
 * @brief 共享内存头部（存储在共享内存开头）
 */
struct ShmHeader {
    uint32_t magic;           // 魔数 "IPCD"
    uint32_t version;         // 版本号
    uint32_t yxCount;         // YX 点位数量
    uint32_t ycCount;         // YC 点位数量
    uint32_t dzCount;         // DZ 点位数量
    uint32_t ykCount;         // YK 点位数量
    uint32_t indexCount;      // 索引条目数量
    uint32_t processCount;    // 注册进程数量
    uint64_t createTime;      // 创建时间
    uint64_t lastUpdateTime;  // 最后更新时间
    
    // 每种类型的当前计数（用于索引分配）
    uint32_t yxIndex;         // YX 当前索引
    uint32_t ycIndex;         // YC 当前索引
    uint32_t dzIndex;         // DZ 当前索引
    uint32_t ykIndex;         // YK 当前索引
    
    // 哈希表大小（避免重新计算）
    uint32_t hashSize;        // 哈希表大小（2的幂）
    uint32_t reserved;        // 保留对齐
    
    // 偏移量（从共享内存起始位置计算）
    uint64_t yxDataOffset;    // YX 数据区偏移
    uint64_t ycDataOffset;    // YC 数据区偏移
    uint64_t dzDataOffset;    // DZ 数据区偏移
    uint64_t ykDataOffset;    // YK 数据区偏移
    uint64_t indexOffset;     // 索引区偏移
    uint64_t processInfoOffset; // 进程信息区偏移
    
    ProcessRWLock lock;       // 跨进程读写锁
};

// ========== YX 数据结构（SoA 布局）==========

struct YXData {
    uint8_t* values;      // 值数组
    uint64_t* timestamps; // 时间戳数组
    uint8_t* qualities;   // 质量码数组
    
    static constexpr size_t VALUE_SIZE = 1;
    static constexpr size_t TIMESTAMP_SIZE = 8;
    static constexpr size_t QUALITY_SIZE = 1;
    static constexpr size_t BYTES_PER_POINT = VALUE_SIZE + TIMESTAMP_SIZE + QUALITY_SIZE; // 10 bytes
};

// ========== YC 数据结构（SoA 布局）==========

struct YCData {
    float* values;        // 值数组
    uint64_t* timestamps; // 时间戳数组
    uint8_t* qualities;   // 质量码数组
    
    static constexpr size_t VALUE_SIZE = 4;
    static constexpr size_t TIMESTAMP_SIZE = 8;
    static constexpr size_t QUALITY_SIZE = 1;
    static constexpr size_t BYTES_PER_POINT = VALUE_SIZE + TIMESTAMP_SIZE + QUALITY_SIZE; // 13 bytes
};

// ========== 数据布局常量 ==========

struct DataLayout {
    static constexpr size_t ALIGNMENT = 8;  // 8字节对齐
    
    // 计算对齐后的偏移
    static constexpr size_t alignUp(size_t size, size_t align) {
        return (size + align - 1) & ~(align - 1);
    }
};

// ========== 共享数据池主类 ==========

/**
 * @brief 共享内存数据池
 * 
 * 特点：
 * - 使用 POSIX 共享内存
 * - SoA (Structure of Arrays) 数据布局，提高缓存效率
 * - 跨进程读写锁保护
 * - 支持百万级点位
 */
class SharedDataPool {
public:
    // ... friend声明
    friend class PersistentStorage;  // 允许持久化存储访问私有成员
    
    /**
     * @brief 创建共享数据池
     * @param name 共享内存名称
     * @param yxCount YX 点位数量
     * @param ycCount YC 点位数量
     * @param dzCount DZ 点位数量
     * @param ykCount YK 点位数量
     */
    static SharedDataPool* create(const char* name,
                                   uint32_t yxCount,
                                   uint32_t ycCount,
                                   uint32_t dzCount,
                                   uint32_t ykCount);
    
    /**
     * @brief 连接到已存在的共享数据池
     */
    static SharedDataPool* connect(const char* name);
    
    /**
     * @brief 销毁共享数据池
     */
    void destroy();
    
    /**
     * @brief 断开连接（不销毁共享内存）
     */
    void disconnect();
    
    // ========== YX 操作 ==========
    
    Result setYX(uint64_t key, uint8_t value, uint64_t timestamp, uint8_t quality = 0);
    Result getYX(uint64_t key, uint8_t& value, uint64_t& timestamp, uint8_t& quality);
    Result setYXByIndex(uint32_t index, uint8_t value, uint64_t timestamp, uint8_t quality = 0);
    Result getYXByIndex(uint32_t index, uint8_t& value, uint64_t& timestamp, uint8_t& quality);
    
    // ========== YC 操作 ==========
    
    Result setYC(uint64_t key, float value, uint64_t timestamp, uint8_t quality = 0);
    Result getYC(uint64_t key, float& value, uint64_t& timestamp, uint8_t& quality);
    Result setYCByIndex(uint32_t index, float value, uint64_t timestamp, uint8_t quality = 0);
    Result getYCByIndex(uint32_t index, float& value, uint64_t& timestamp, uint8_t& quality);
    
    // ========== DZ 操作 ==========
    
    Result setDZ(uint64_t key, float value, uint64_t timestamp, uint8_t quality = 0);
    Result getDZ(uint64_t key, float& value, uint64_t& timestamp, uint8_t& quality);
    
    // ========== YK 操作 ==========
    
    Result setYK(uint64_t key, uint8_t value, uint64_t timestamp, uint8_t quality = 0);
    Result getYK(uint64_t key, uint8_t& value, uint64_t& timestamp, uint8_t& quality);
    
    // ========== 索引操作 ==========
    
    Result registerKey(uint64_t key, PointType type, uint32_t& outIndex);
    Result findKey(uint64_t key, PointType& type, uint32_t& index);
    
    // ========== 进程管理 ==========
    
    Result registerProcess(const char* name, uint32_t& processId);
    Result unregisterProcess(uint32_t processId);
    Result updateHeartbeat(uint32_t processId);
    Result getProcessInfo(uint32_t processId, ProcessInfo& info);
    
    // ========== 批量操作 ==========
    
    /**
     * @brief 批量设置YX数据
     * @param keys 键数组
     * @param values 值数组
     * @param timestamps 时间戳数组
     * @param count 数量
     * @param successCount [out] 成功数量（可选）
     * @return Result::OK 操作完成（可能部分失败）
     */
    Result batchSetYX(const uint64_t* keys, const uint8_t* values, 
                      const uint64_t* timestamps, uint32_t count,
                      uint32_t* successCount = nullptr);
    
    /**
     * @brief 批量设置YC数据
     * @param keys 键数组
     * @param values 值数组
     * @param timestamps 时间戳数组
     * @param count 数量
     * @param successCount [out] 成功数量（可选）
     * @return Result::OK 操作完成（可能部分失败）
     */
    Result batchSetYC(const uint64_t* keys, const float* values,
                      const uint64_t* timestamps, uint32_t count,
                      uint32_t* successCount = nullptr);
    
    // ========== 信息查询 ==========
    
    bool isValid() const { return m_shmPtr != nullptr && m_header != nullptr; }
    const ShmHeader* getHeader() const { return m_header; }
    uint32_t getYXCount() const { return m_header ? m_header->yxCount : 0; }
    uint32_t getYCCount() const { return m_header ? m_header->ycCount : 0; }
    uint32_t getDZCount() const { return m_header ? m_header->dzCount : 0; }
    uint32_t getYKCount() const { return m_header ? m_header->ykCount : 0; }
    const char* getName() const { return m_name.c_str(); }
    
    // ========== 锁操作 ==========
    
    void lockRead() { if (m_header) m_header->lock.readLock(); }
    void unlockRead() { if (m_header) m_header->lock.unlock(); }
    void lockWrite() { if (m_header) m_header->lock.writeLock(); }
    void unlockWrite() { if (m_header) m_header->lock.unlock(); }
    
    // ========== 统计功能 ==========
    
    /**
     * @brief 获取统计信息
     */
    DataPoolStats getStats() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    /**
     * @brief 增加读取计数
     */
    void incrementReadCount();
    
    /**
     * @brief 增加写入计数
     */
    void incrementWriteCount(PointType type);
    
    // ========== 健康检查 ==========
    
    /**
     * @brief 检查进程健康状态
     */
    ProcessHealth checkProcessHealth(uint32_t processId) const;
    
    /**
     * @brief 检查进程健康状态（通过PID）
     */
    ProcessHealth checkProcessHealthByPid(pid_t pid) const;
    
    /**
     * @brief 获取活跃进程列表
     */
    uint32_t getActiveProcessList(uint32_t* processIds, uint32_t maxCount) const;
    
    /**
     * @brief 清理死亡进程
     */
    uint32_t cleanupDeadProcesses();
    
    // ========== 快照/持久化 ==========
    
    /**
     * @brief 保存快照到文件
     * @param filename 文件路径
     * @return Result::OK 成功
     */
    Result saveSnapshot(const char* filename);
    
    /**
     * @brief 从文件加载快照
     * @param filename 文件路径
     * @return Result::OK 成功
     */
    Result loadSnapshot(const char* filename);
    
    /**
     * @brief 验证快照文件
     * @param filename 文件路径
     * @return true 文件有效
     */
    bool validateSnapshot(const char* filename);

private:
    SharedDataPool() = default;
    
    // 禁止拷贝
    SharedDataPool(const SharedDataPool&) = delete;
    SharedDataPool& operator=(const SharedDataPool&) = delete;
    
public:
    ~SharedDataPool() = default;
    
    // 内部方法
    bool initFromShm();
    void cleanup();
    size_t calculateRequiredSize(uint32_t yxCount, uint32_t ycCount,
                                  uint32_t dzCount, uint32_t ykCount);
    
    // 哈希索引
    uint32_t hashKey(uint64_t key) const;
    
private:
    std::string m_name;       // 共享内存名称
    void* m_shmPtr;           // 共享内存指针
    size_t m_shmSize;         // 共享内存大小
    int m_shmFd;              // 共享内存文件描述符
    bool m_isCreator;         // 是否为创建者
    
    ShmHeader* m_header;      // 头部指针
    
    // SoA 数据指针
    YXData m_yxData;
    YCData m_ycData;
    YCData m_dzData;          // DZ 与 YC 结构相同
    YXData m_ykData;          // YK 与 YX 结构相同
    
    IndexEntry* m_indexTable; // 索引表
    uint32_t* m_hashTable;    // 哈希表（存储索引偏移）
    uint32_t m_hashSize;      // 哈希表大小
    
    ProcessInfo* m_processInfo; // 进程信息数组
    
    // 本地统计（不存共享内存）
    mutable DataPoolStats m_stats;
};

} // namespace IPC

#endif // SHARED_DATA_POOL_H
