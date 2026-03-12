/**
 * @file LogService.h
 * @brief 日志服务头文件
 *
 * 提供 IEC61850 标准的日志记录功能。
 */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <iec61850_server.h>

namespace IPC {

/**
 * @brief 日志条目类型
 */
enum class LogEntryType {
    EVENT,      // 事件日志
    SOE,         // 序列化事件日志
    DIAGNOSTIC,  // 诊断日志
    AUDIT        // 审计日志
};

/**
 * @brief 日志条目
 */
struct LogEntry {
    LogEntryType type;           // 条目类型
    std::string entryId;          // 条目标识符
    std::string objectRef;         // IEC61850 对象引用
    MmsValue* value;             // 值 (由 LogService 管理)
    uint16_t quality;             // 质量码
    uint64_t timestamp;           // 时间戳（纳秒）
    std::string reason;            // 原因/触发源
};

/**
 * @brief 日志统计信息
 */
struct LogServiceStats {
    uint64_t totalEntries;        // 总条目数
    uint64_t eventCount;          // 事件日志数
    uint64_t soeCount;            // SOE 日志数
    uint32_t currentEntries;      // 当前条目数
    uint32_t maxEntries;           // 最大条目数
};

/**
 * @brief SOE 事件定义
 */
struct SOEEvent {
    uint64_t dataKey;            // 数据点键
    uint8_t oldValue;             // 旧值
    uint8_t newValue;             // 新值
    uint64_t timestamp;           // 时间戳
    uint16_t quality;             // 质量码
};

/**
 * @brief 日志回调函数类型
 */
using LogCallback = std::function<void(const LogEntry& entry)>;

/**
 * @brief 日志服务类
 *
 * 提供符合 IEC61850 标准的日志记录功能，
 * 支持事件、SOE、诊断等类型日志。
 */
class LogService {
public:
    /**
     * @brief 日志服务配置
     */
    struct LogConfig {
        std::string logName;            // 日志名称
        std::string lnRef;              // 逻辑节点引用
        uint32_t maxEntries;           // 最大条目数 (0=无限制)
        bool enableSOE;                // 是否启用 SOE 记录
        uint32_t soeMaxEntries;        // SOE 最大条目数
    };

    /**
     * @brief 创建日志服务
     * @param config 日志服务配置
     * @return 成功返回指针，失败返回 nullptr
     */
    static LogService* create(const LogConfig& config);

    /**
     * @brief 销毁日志服务
     */
    void destroy();

    /**
     * @brief 检查日志服务是否已初始化
     */
    bool isInitialized() const { return config_.maxEntries > 0; }

    /**
     * @brief 记录事件日志
     * @param entryId 条目标识符
     * @param objectRef IEC61850 对象引用
     * @param value 值
     * @param quality 质量码
     * @param timestamp 时间戳（纳秒）
     * @param reason 原因
     * @return 成功返回 true
     */
    bool logEvent(const std::string& entryId, const std::string& objectRef,
                  MmsValue* value, uint16_t quality,
                  uint64_t timestamp, const std::string& reason = "");

    /**
     * @brief 记录 SOE 事件
     * @param key 数据点键
     * @param oldValue 旧值
     * @param newValue 新值
     * @param timestamp 时间戳
     * @param quality 质量码
     * @return 成功返回 true
     */
    bool logSOE(uint64_t key, uint8_t oldValue, uint8_t newValue,
                  uint64_t timestamp, uint16_t quality);

    /**
     * @brief 记录诊断日志
     * @param entryId 条目标识符
     * @param message 诊断消息
     * @return 成功返回 true
     */
    bool logDiagnostic(const std::string& entryId, const std::string& message,
                          uint64_t timestamp);

    /**
     * @brief 记录审计日志
     * @param entryId 条目标识符
     * @param action 审计操作
     * @param user 用户标识
     * @param timestamp 时间戳
     * @return 成功返回 true
     */
    bool logAudit(const std::string& entryId, const std::string& action,
                     const std::string& user, uint64_t timestamp);

    /**
     * @brief 查询日志条目
     * @param startIndex 起始索引
     * @param count 查询数量
     * @return 日志条目列表
     */
    std::vector<LogEntry> queryLogs(uint32_t startIndex = 0, uint32_t count = 0);

    /**
     * @brief 清空日志
     * @return 清空的条目数量
     */
    int clearLogs();

    /**
     * @brief 清空 SOE 日志
     * @return 清空的条目数量
     */
    int clearSOELogs();

    /**
     * @brief 获取统计信息
     * @return 统计信息
     */
    LogServiceStats getStats() const { return stats_; }

    /**
     * @brief 重置统计信息
     */
    void resetStats();

    /**
     * @brief 设置日志回调
     * @param callback 日志回调函数
     */
    void setLogCallback(LogCallback callback) {
        logCallback_ = callback;
    }

    /**
     * @brief 获取最大条目数
     */
    uint32_t getMaxEntries() const { return config_.maxEntries; }

private:
    LogService(const LogConfig& config);
    ~LogService();

    // 禁止拷贝
    LogService(const LogService&) = delete;
    LogService& operator=(const LogService&) = delete;

    /**
     * @brief 初始化日志服务
     */
    bool initialize();

    /**
     * @brief 清理值
     */
    void cleanupEntries();

private:
    LogConfig config_;                    // 配置信息
    std::deque<LogEntry> entries_;       // 日志条目（主日志）
    std::deque<SOEEvent> soeEntries_;    // SOE 条目（单独存储）

    LogServiceStats stats_;                  // 统计信息
    std::mutex entriesMutex_;               // 条目访问锁
    std::mutex soeMutex_;                  // SOE 访问锁

    LogCallback logCallback_;                // 日志回调
};

} // namespace IPC

#endif // HAS_LIBIEC61850
#endif // LOG_SERVICE_H
