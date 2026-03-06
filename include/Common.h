#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstring>
#include <string>
#include <ctime>
#include <unistd.h>

namespace IPC {

// ========== 常量定义 ==========

constexpr uint32_t SHM_MAGIC = 0x49504344;  // "IPCD"
constexpr uint32_t SHM_VERSION = 1;

constexpr size_t MAX_POINTS_PER_TYPE = 1000000;   // 每类最大点位数
constexpr size_t MAX_EVENT_BUFFER_SIZE = 10000;   // 事件缓冲区大小
constexpr size_t MAX_PROCESS_COUNT = 32;          // 最大进程数
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;  // 心跳发送间隔
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 5000;   // 心跳超时时间
constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 2000; // 健康检查间隔
constexpr uint32_t LOCK_TIMEOUT_MS = 3000;        // 锁超时时间
constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;    // 无效索引

// ========== 点位类型枚举 ==========

enum class PointType : uint8_t {
    YX = 0,     // 遥信
    YC = 1,     // 遥测
    DZ = 2,     // 定值
    YK = 3      // 遥控
};

// ========== 结果码 ==========

enum class Result : int {
    OK = 0,
    ERROR = -1,
    INVALID_PARAM = -2,
    NOT_INITIALIZED = -3,
    ALREADY_EXISTS = -4,
    NOT_FOUND = -5,
    BUFFER_FULL = -6,
    TIMEOUT = -7,
    PERMISSION_DENIED = -8,
    OUT_OF_MEMORY = -9
};

// ========== 工具函数 ==========

/**
 * @brief 构造数据点 key
 * @param addr 设备地址
 * @param id 数据点 ID
 * @return 64位 key
 */
inline uint64_t makeKey(int addr, int id) {
    return (static_cast<uint64_t>(addr) << 32) | 
           static_cast<uint32_t>(id);
}

/**
 * @brief 从 key 提取地址
 */
inline int getKeyAddr(uint64_t key) {
    return static_cast<int>(key >> 32);
}

/**
 * @brief 从 key 提取 ID
 */
inline int getKeyId(uint64_t key) {
    return static_cast<int>(key & 0xFFFFFFFF);
}

/**
 * @brief 获取当前时间戳（毫秒）
 */
inline uint64_t getCurrentTimestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + 
           static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}

/**
 * @brief 计算CRC32校验和（运行时生成查找表）
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC32校验和
 */
inline uint32_t calculateChecksum(const void* data, size_t len) {
    // CRC32 多项式（反向）
    const uint32_t POLY = 0xEDB88320;
    
    // 运行时计算 CRC（或可用预计算表优化）
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? POLY : 0);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ========== 进程信息结构 ==========

/**
 * @brief 进程注册信息（存储在共享内存中）
 * 
 * 注意：此结构体存储在共享内存中，不应包含 std::atomic 等非 POD 类型。
 * 所有成员都应使用基本类型，原子性通过代码逻辑保证。
 */
struct ProcessInfo {
    pid_t pid;                      ///< 进程ID
    uint64_t lastHeartbeat;         ///< 最后心跳时间
    uint32_t eventReadIndex;        ///< 事件读取位置
    char name[32];                  ///< 进程名称
    volatile bool active;           ///< 是否活跃（使用 volatile 确保可见性）
    
    ProcessInfo() : pid(0), lastHeartbeat(0), eventReadIndex(0), active(false) {
        std::memset(name, 0, sizeof(name));
    }
    
    void setName(const char* n) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
};

// ========== 事件结构 ==========

/**
 * @brief 跨进程事件（固定大小，便于共享内存存储）
 * 
 * 使用 pragma pack 确保 64 字节对齐
 */
#pragma pack(push, 1)
struct Event {
    uint64_t key;               ///< 数据点 key (8 bytes)
    int32_t addr;               ///< 设备地址 (4 bytes)
    int32_t id;                 ///< 数据点 ID (4 bytes)
    PointType pointType;        ///< 点位类型 (1 byte)
    uint8_t reserved;           ///< 保留对齐 (1 byte)
    
    union {                     ///< 旧值 (4 bytes)
        float floatValue;       ///< YC/DZ 值
        uint32_t intValue;      ///< YX/YK 值
    } oldValue;
    
    union {                     ///< 新值 (4 bytes)
        float floatValue;
        uint32_t intValue;
    } newValue;
    
    uint64_t timestamp;         ///< 事件时间戳 (8 bytes)
    uint8_t quality;            ///< 质量码 (1 byte)
    uint8_t isCritical;         ///< 是否关键点 (1 byte)
    uint16_t sourcePid;         ///< 来源进程 PID (2 bytes)
    char source[16];            ///< 来源标识 (16 bytes)
    uint8_t padding[10];        ///< 填充到 64 字节 (10 bytes)
    
    Event() : key(0), addr(0), id(0), pointType(PointType::YX),
              reserved(0), timestamp(0), quality(0), 
              isCritical(0), sourcePid(0) {
        oldValue.intValue = 0;
        newValue.intValue = 0;
        std::memset(source, 0, sizeof(source));
        std::memset(padding, 0, sizeof(padding));
    }
};
#pragma pack(pop)

static_assert(sizeof(Event) == 64, "Event size must be 64 bytes");

// ========== 数据结构（SoA 布局）==========

/**
 * @brief YX 数据布局（存储在共享内存中）
 */
struct YXDataLayout {
    // uint8_t* values;       // 值数组
    // uint64_t* timestamps;  // 时间戳数组
    // uint8_t* qualities;    // 质量码数组
    // 每点 10 bytes
    static constexpr size_t BYTES_PER_POINT = sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint8_t);
};

/**
 * @brief YC 数据布局（存储在共享内存中）
 */
struct YCDataLayout {
    // float* values;         // 值数组
    // uint64_t* timestamps;  // 时间戳数组
    // uint8_t* qualities;    // 质量码数组
    // 每点 13 bytes
    static constexpr size_t BYTES_PER_POINT = sizeof(float) + sizeof(uint64_t) + sizeof(uint8_t);
};

/**
 * @brief DZ 数据布局（存储在共享内存中）
 */
struct DZDataLayout {
    static constexpr size_t BYTES_PER_POINT = sizeof(float) + sizeof(uint64_t) + sizeof(uint8_t);
};

/**
 * @brief YK 数据布局（存储在共享内存中）
 */
struct YKDataLayout {
    static constexpr size_t BYTES_PER_POINT = sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint8_t);
};

// ========== 索引映射结构 ==========

/**
 * @brief 索引映射条目
 */
struct IndexEntry {
    uint64_t key;           ///< 数据点 key
    uint32_t index;         ///< 类型内索引
    uint32_t next;          ///< 哈希链表下一节点偏移
    PointType type;         ///< 点位类型
    uint8_t reserved[3];    ///< 保留对齐
};

// ========== 统计信息结构 ==========

/**
 * @brief 数据池统计信息
 */
struct DataPoolStats {
    uint64_t totalReads;        ///< 总读取次数
    uint64_t totalWrites;       ///< 总写入次数
    uint64_t yxWrites;          ///< YX写入次数
    uint64_t ycWrites;          ///< YC写入次数
    uint64_t dzWrites;          ///< DZ写入次数
    uint64_t ykWrites;          ///< YK写入次数
    uint64_t eventPublishes;    ///< 事件发布次数
    uint64_t eventProcesses;    ///< 事件处理次数
    uint64_t lastResetTime;     ///< 上次重置时间
    uint32_t activeProcessCount; ///< 活跃进程数
    
    DataPoolStats() : totalReads(0), totalWrites(0), yxWrites(0),
                      ycWrites(0), dzWrites(0), ykWrites(0),
                      eventPublishes(0), eventProcesses(0),
                      lastResetTime(0), activeProcessCount(0) {}
};

/**
 * @brief 进程健康状态
 */
enum class ProcessHealth : uint8_t {
    HEALTHY = 0,        ///< 健康（心跳正常）
    WARNING = 1,        ///< 警告（心跳延迟）
    DEAD = 2,           ///< 死亡（心跳超时）
    UNKNOWN = 3         ///< 未知（未注册）
};

/**
 * @brief 快照文件头部
 */
#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic;             // 魔数 "ISNP" (IPC Snapshot)
    uint32_t version;           // 版本号
    uint32_t yxCount;           // YX 点数
    uint32_t ycCount;           // YC 点数
    uint32_t dzCount;           // DZ 点数
    uint32_t ykCount;           // YK 点数
    uint32_t indexCount;        // 索引条目数
    uint32_t checksum;          // 校验和
    uint64_t snapshotTime;      // 快照时间
    uint64_t shmSize;           // 原始共享内存大小
    uint8_t reserved[48];       // 保留
};
#pragma pack(pop)

static_assert(sizeof(SnapshotHeader) == 96, "SnapshotHeader size mismatch");

} // namespace IPC

#endif // IPC_COMMON_H
