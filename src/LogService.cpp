/**
 * @file LogService.cpp
 * @brief 日志服务实现
 */

#ifdef HAS_LIBIEC61850

#include "LogService.h"
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace IPC {

LogService::LogService(const LogConfig& config)
    : config_(config)
{
    // 初始化统计信息
    memset(&stats_, 0, sizeof(stats_));
}

LogService::~LogService() {
    destroy();
}

LogService* LogService::create(const LogConfig& config) {
    // 验证配置
    if (config.maxEntries == 0) {
        std::cerr << "Invalid LogConfig: maxEntries must be > 0" << std::endl;
        return nullptr;
    }

    LogService* service = new LogService(config);
    if (!service->initialize()) {
        delete service;
        return nullptr;
    }

    std::cout << "LogService created: " << config.logName << std::endl;
    return service;
}

void LogService::destroy() {
    cleanupEntries();
}

bool LogService::initialize() {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries_.clear();
    soeEntries_.clear();

    return true;
}

bool LogService::logEvent(const std::string& entryId, const std::string& objectRef,
                       MmsValue* value, uint16_t quality,
                       uint64_t timestamp, const std::string& reason) {
    if (config_.maxEntries > 0 && entries_.size() >= config_.maxEntries) {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        // 移除最旧的条目
        entries_.pop_front();
        stats_.droppedEntries++;
    }

    // 克隆值（保持所有权）
    MmsValue* clonedValue = nullptr;
    if (value) {
        clonedValue = MmsValue_clone(value);
    }

    // 创建日志条目
    LogEntry entry;
    entry.type = LogEntryType::EVENT;
    entry.entryId = entryId;
    entry.objectRef = objectRef;
    entry.value = clonedValue;
    entry.quality = quality;
    entry.timestamp = timestamp;
    entry.reason = reason;

    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries_.push_back(entry);

    // 更新统计
    stats_.totalEntries++;
    stats_.eventCount++;
    stats_.currentEntries = static_cast<uint32_t>(entries_.size());

    // 调用回调
    if (logCallback_) {
        logCallback_(entry);
    }

    // 释放克隆的值（在回调后可能需要保留）
    if (clonedValue) {
        MmsValue_delete(clonedValue);
    }

    std::cout << "Event logged: " << entryId << std::endl;
    return true;
}

bool LogService::logSOE(uint64_t key, uint8_t oldValue, uint8_t newValue,
                   uint64_t timestamp, uint16_t quality) {
    if (!config_.enableSOE) {
        return false;
    }

    std::lock_guard<std::mutex> soeLock(soeMutex_);

    // 检查 SOE 条目数限制
    if (config_.soeMaxEntries > 0 && soeEntries_.size() >= config_.soeMaxEntries) {
        // 移除最旧的 SOE 条目
        soeEntries_.pop_front();
    }

    // 创建 SOE 事件
    SOEEvent soe;
    soe.dataKey = key;
    soe.oldValue = oldValue;
    soe.newValue = newValue;
    soe.timestamp = timestamp;
    soe.quality = quality;

    soeEntries_.push_back(soe);

    // 更新统计
    stats_.soeCount++;

    std::cout << "SOE logged for key: " << key
              << " (0x" << std::hex << static_cast<int>(oldValue)
              << "->0x" << std::hex << static_cast<int>(newValue) << ")"
              << std::endl;

    return true;
}

bool LogService::logDiagnostic(const std::string& entryId, const std::string& message,
                                uint64_t timestamp) {
    if (config_.maxEntries > 0 && entries_.size() >= config_.maxEntries) {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        entries_.pop_front();
        stats_.droppedEntries++;
    }

    LogEntry entry;
    entry.type = LogEntryType::DIAGNOSTIC;
    entry.entryId = entryId;
    entry.objectRef = "";  // 诊断日志不需要对象引用
    entry.value = nullptr;
    entry.quality = 0;
    entry.timestamp = timestamp;
    entry.reason = message;

    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries_.push_back(entry);

    stats_.totalEntries++;
    stats_.currentEntries = static_cast<uint32_t>(entries_.size());

    if (logCallback_) {
        logCallback_(entry);
    }

    std::cout << "Diagnostic logged: " << entryId << std::endl;
    return true;
}

bool LogService::logAudit(const std::string& entryId, const std::string& action,
                     const std::string& user, uint64_t timestamp) {
    if (config_.maxEntries > 0 && entries_.size() >= config_.maxEntries) {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        entries_.pop_front();
        stats_.droppedEntries++;
    }

    LogEntry entry;
    entry.type = LogEntryType::AUDIT;
    entry.entryId = entryId;
    entry.objectRef = "";
    entry.value = nullptr;
    entry.quality = 0;
    entry.timestamp = timestamp;
    entry.reason = "Action: " + action + ", User: " + user;

    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries_.push_back(entry);

    stats_.totalEntries++;
    stats_.currentEntries = static_cast<uint32_t>(entries_.size());

    if (logCallback_) {
        logCallback_(entry);
    }

    std::cout << "Audit logged: " << entryId << std::endl;
    return true;
}

std::vector<LogEntry> LogService::queryLogs(uint32_t startIndex, uint32_t count) {
    std::lock_guard<std::mutex> lock(entriesMutex_);

    std::vector<LogEntry> result;
    size_t totalEntries = entries_.size();
    size_t endIndex = (count == 0) ? totalEntries : std::min(totalEntries, static_cast<size_t>(startIndex + count));

    for (size_t i = startIndex; i < endIndex; ++i) {
        result.push_back(entries_[i]);
    }

    return result;
}

int LogService::clearLogs() {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    int count = static_cast<int>(entries_.size());
    entries_.clear();
    stats_.currentEntries = 0;
    stats_.droppedEntries = 0;

    std::cout << "Cleared " << count << " log entries" << std::endl;
    return count;
}

int LogService::clearSOELogs() {
    std::lock_guard<std::mutex> soeLock(soeMutex_);
    int count = static_cast<int>(soeEntries_.size());
    soeEntries_.clear();
    stats_.soeCount = 0;

    std::cout << "Cleared " << count << " SOE entries" << std::endl;
    return count;
}

void LogService::resetStats() {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    memset(&stats_, 0, sizeof(stats_));
    stats_.currentEntries = static_cast<uint32_t>(entries_.size());

    std::cout << "Statistics reset" << std::endl;
}

void LogService::setLogCallback(LogCallback callback) {
    logCallback_ = callback;
}

void LogService::cleanupEntries() {
    for (auto& entry : entries_) {
        if (entry.value) {
            MmsValue_delete(entry.value);
        }
    }
    entries_.clear();

    // 注意：SOE 条目不包含 value 指针，是原始值
}

} // namespace IPC

#endif // HAS_LIBIEC61850
