#ifndef IPC_SOE_RECORDER_H
#define IPC_SOE_RECORDER_H

#include "Common.h"
#include "ProcessRWLock.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace IPC {

// ========== SOE 常量定义 ==========

constexpr size_t MAX_SOE_RECORDS = 100000;      // 最大SOE记录数（环形缓冲）
constexpr uint32_t SOE_MAGIC = 0x534F4500;       // "SOE\0"
constexpr uint32_t SOE_VERSION = 1;

// ========== SOE 事件质量码 ==========

enum class SOEQuality : uint8_t {
    VALID = 0,              ///< 有效
    INVALID = 1,            ///< 无效
    QUESTIONABLE = 2,       ///< 可疑
    RESERVE_OVERFLOW = 3    ///< 缓冲区溢出
};

// ========== SOE 事件类型 ==========

enum class SOEEventType : uint8_t {
    YX_CHANGE = 0,          ///< 遥信变位
    YK_EXECUTE = 1,         ///< 遥控执行
    YK_FEEDBACK = 2,        ///< 遥控返校
    ALARM_TRIGGER = 3,      ///< 告警触发
    ALARM_CLEAR = 4,        ///< 告警复归
    PROTECTION_ACT = 5,     ///< 保护动作
    FAULT_RECORD = 6        ///< 故障录波触发
};

// ========== SOE 记录结构 ==========
// 要求分辨率 < 1ms，使用纳秒级时标

#pragma pack(push, 1)
struct SOERecord {
    uint64_t absoluteTime;      ///< 绝对时标（纳秒，Unix时间）
    uint64_t monotonicTime;     ///< 单调时钟时标（纳秒）
    uint32_t pointKey;          ///< 点位标识（设备地址 << 16 | 点号）
    uint16_t msoc;              ///< 毫秒计数器（0-999，用于分辨率校验）
    uint16_t sourcePid;         ///< 来源进程PID
    uint8_t pointType;          ///< 点位类型（PointType枚举值）
    uint8_t eventType;          ///< 事件类型（SOEEventType枚举值）
    uint8_t quality;            ///< 质量码（SOEQuality枚举值）
    uint8_t oldValue;           ///< 旧值
    uint8_t newValue;           ///< 新值
    uint8_t priority;           ///< 优先级（0-255，越高越优先）
    uint8_t reserved[2];        ///< 保留对齐
    
    SOERecord() : absoluteTime(0), monotonicTime(0), pointKey(0), 
                  msoc(0), sourcePid(0), pointType(0), eventType(0),
                  quality(static_cast<uint8_t>(SOEQuality::VALID)),
                  oldValue(0), newValue(0), priority(0) {
        std::memset(reserved, 0, sizeof(reserved));
    }
};
#pragma pack(pop)

static_assert(sizeof(SOERecord) == 32, "SOERecord size must be 32 bytes");

// ========== SOE 缓冲区头部 ==========

struct SOEBufferHeader {
    uint32_t magic;             ///< 魔数
    uint32_t version;           ///< 版本号
    uint32_t capacity;          ///< 缓冲区容量
    std::atomic<uint32_t> head; ///< 写入位置（环形缓冲）
    std::atomic<uint32_t> tail; ///< 读取位置
    std::atomic<uint64_t> totalRecords;   ///< 总记录数
    std::atomic<uint64_t> droppedRecords; ///< 丢弃记录数
    std::atomic<uint64_t> lastRecordTime; ///< 最后记录时间（纳秒）
    ProcessRWLock lock;         ///< 跨进程锁
    uint8_t reserved[64];       ///< 保留空间
    
    SOEBufferHeader() : magic(SOE_MAGIC), version(SOE_VERSION),
                        capacity(0), head(0), tail(0),
                        totalRecords(0), droppedRecords(0),
                        lastRecordTime(0) {
        std::memset(reserved, 0, sizeof(reserved));
    }
};

// ========== SOE 查询条件 ==========

struct SOEQueryCondition {
    uint64_t startTime;         ///< 开始时间（纳秒，0表示不限制）
    uint64_t endTime;           ///< 结束时间（纳秒，0表示不限制）
    uint32_t pointKey;          ///< 点位标识（0表示全部）
    uint8_t pointType;          ///< 点位类型（0xFF表示全部）
    uint8_t eventType;          ///< 事件类型（0xFF表示全部）
    uint8_t minPriority;        ///< 最低优先级（0表示全部）
    uint32_t maxRecords;        ///< 最大返回记录数
    bool reverseOrder;          ///< 是否逆序（最新在前）
    
    SOEQueryCondition() : startTime(0), endTime(0), pointKey(0),
                          pointType(0xFF), eventType(0xFF),
                          minPriority(0), maxRecords(1000),
                          reverseOrder(true) {}
};

// ========== SOE 统计信息 ==========

struct SOEStats {
    uint64_t totalRecords;      ///< 总记录数
    uint64_t droppedRecords;    ///< 丢弃记录数
    uint64_t lastRecordTime;    ///< 最后记录时间
    uint32_t currentLoad;       ///< 当前缓冲区使用量
    float loadPercent;          ///< 缓冲区使用率
    uint32_t capacity;          ///< 缓冲区容量
    
    SOEStats() : totalRecords(0), droppedRecords(0), lastRecordTime(0),
                 currentLoad(0), loadPercent(0.0f), capacity(0) {}
};

// ========== SOE 记录器类 ==========

/**
 * @brief SOE事件顺序记录器
 * 
 * 特点：
 * - 纳秒级时标精度，分辨率 < 1ms
 * - 环形缓冲区，支持高频率事件
 * - 跨进程安全访问
 * - 支持条件查询和过滤
 * - 支持导出为标准格式
 */
class SOERecorder {
public:
    /**
     * @brief 创建SOE记录器
     * @param name 共享内存名称
     * @param capacity 缓冲区容量
     */
    static SOERecorder* create(const char* name, uint32_t capacity = MAX_SOE_RECORDS);
    
    /**
     * @brief 连接到已存在的SOE记录器
     */
    static SOERecorder* connect(const char* name);
    
    /**
     * @brief 销毁SOE记录器
     */
    void destroy();
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    // ========== 记录操作 ==========
    
    /**
     * @brief 记录SOE事件
     * @param record SOE记录
     * @return Result::OK 成功
     */
    Result record(const SOERecord& record);
    
    /**
     * @brief 记录遥信变位事件（快捷方法）
     * @param pointKey 点位标识
     * @param oldValue 旧值
     * @param newValue 新值
     * @param priority 优先级
     * @return Result::OK 成功
     */
    Result recordYXChange(uint32_t pointKey, uint8_t oldValue, uint8_t newValue, 
                          uint8_t priority = 128);
    
    /**
     * @brief 记录遥控执行事件（快捷方法）
     */
    Result recordYKExecute(uint32_t pointKey, uint8_t command, 
                           uint8_t priority = 200);
    
    /**
     * @brief 记录保护动作事件（快捷方法）
     */
    Result recordProtectionAct(uint32_t pointKey, uint8_t action,
                               uint8_t priority = 255);
    
    // ========== 查询操作 ==========
    
    /**
     * @brief 查询SOE记录
     * @param condition 查询条件
     * @param records 输出记录数组
     * @param count 输出记录数
     * @param maxCount 数组最大容量
     * @return Result::OK 成功
     */
    Result query(const SOEQueryCondition& condition,
                 SOERecord* records, uint32_t& count, uint32_t maxCount);
    
    /**
     * @brief 获取最新N条记录
     */
    Result getLatest(uint32_t count, SOERecord* records, uint32_t& actualCount);
    
    /**
     * @brief 获取指定时间范围内的记录
     */
    Result getByTimeRange(uint64_t startTime, uint64_t endTime,
                          SOERecord* records, uint32_t& count, uint32_t maxCount);
    
    // ========== 统计与监控 ==========
    
    /**
     * @brief 获取统计信息
     */
    SOEStats getStats() const;
    
    /**
     * @brief 清空缓冲区
     */
    void clear();
    
    /**
     * @brief 检查是否有效
     */
    bool isValid() const { return m_shmPtr != nullptr && m_header != nullptr; }
    
    // ========== 导出功能 ==========
    
    /**
     * @brief 导出为CSV格式
     * @param filename 文件路径
     * @param condition 导出条件（可选）
     * @return Result::OK 成功
     */
    Result exportToCSV(const char* filename, 
                       const SOEQueryCondition* condition = nullptr);
    
    /**
     * @brief 导出为COMTRADE格式（用于故障分析）
     * @param filename 文件路径前缀（会生成.cfg和.dat文件）
     * @param triggerTime 触发时间
     * @param preTime 触发前时间（毫秒）
     * @param postTime 触发后时间（毫秒）
     * @return Result::OK 成功
     */
    Result exportToCOMTRADE(const char* filename, uint64_t triggerTime,
                            uint32_t preTime = 1000, uint32_t postTime = 1000);
    
    // ========== 锁操作 ==========
    
    void lockRead() { if (m_header) m_header->lock.readLock(); }
    void unlockRead() { if (m_header) m_header->lock.unlock(); }
    void lockWrite() { if (m_header) m_header->lock.writeLock(); }
    void unlockWrite() { if (m_header) m_header->lock.unlock(); }
    
private:
    SOERecorder() = default;
    SOERecorder(const SOERecorder&) = delete;
    SOERecorder& operator=(const SOERecorder&) = delete;
    
    bool initFromShm();
    size_t calculateRequiredSize(uint32_t capacity);
    
private:
    std::string m_name;         ///< 共享内存名称
    void* m_shmPtr;             ///< 共享内存指针
    size_t m_shmSize;           ///< 共享内存大小
    int m_shmFd;                ///< 文件描述符
    bool m_isCreator;           ///< 是否为创建者
    
    SOEBufferHeader* m_header;  ///< 缓冲区头部
    SOERecord* m_records;       ///< 记录数组
};

// ========== 时间工具函数 ==========

/**
 * @brief 获取当前时间（纳秒，Unix时间）
 */
inline uint64_t getAbsoluteTimeNs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * @brief 获取单调时钟时间（纳秒）
 */
inline uint64_t getMonotonicTimeNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * @brief 获取毫秒计数器（0-999）
 */
inline uint16_t getMsoc() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint16_t>(ts.tv_nsec / 1000000);
}

/**
 * @brief 纳秒时间转换为字符串
 */
inline std::string timeNsToString(uint64_t timeNs) {
    time_t seconds = timeNs / 1000000000ULL;
    uint32_t nanos = timeNs % 1000000000ULL;
    struct tm* tm_info = localtime(&seconds);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    char result[80];
    snprintf(result, sizeof(result), "%s.%09u", buffer, nanos);
    return std::string(result);
}

} // namespace IPC

#endif // IPC_SOE_RECORDER_H
