/**
 * @file ReportAdapter.h
 * @brief 报告控制块适配器
 *
 * 基于 libiec61850 实现的 BRCB/URCB 适配器，
 * 提供面向对象的报告管理接口。
 */

#ifndef REPORT_ADAPTER_H
#define REPORT_ADAPTER_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <iec61850_server.h>

namespace IPC {

/**
 * @brief 报告控制块类型
 */
enum class ReportType {
    BUFFERED,      // 缓冲报告控制块 (BRCB)
    UNBUFFERED,    // 非缓冲报告控制块 (URCB)
    LOG_CONTROL    // 日志控制块 (LCB)
};

/**
 * @brief 报告触发选项
 */
enum class ReportTriggerOptions {
    DATA_CHANGE = 0x01,      // 数据变化
    GI = 0x02,                  // 通用完整性重置
    INTEGRITY = 0x04,           // 完整性检查
    QUALITY_CHANGE = 0x08,       // 质量变化
    TIMER = 0x10                // 定时器触发
};

/**
 * @brief 报告可选域标志
 */
enum class ReportOptions {
    SEQ_NUM = 0x01,             // 序列号
    TIME_STAMP = 0x02,          // 时间戳
    REASON_CODE = 0x04,          // 原因代码
    DATA_SET = 0x08              // 数据集
    BUF_OVERFLOW = 0x10,          // 缓冲区溢出
    ENTRY_ID = 0x20,             // 条目标识
    CONF_REV = 0x40              // 配置版本
};

/**
 * @brief 报告数据条目
 */
struct ReportEntry {
    std::string objectRef;        // 对象引用
    MmsValue* value;             // 值
    uint64_t timestamp;           // 时间戳
    uint16_t quality;             // 质量码
};

/**
 * @brief 报告控制块配置
 */
struct ReportControlConfig {
    ReportType type;               // 控制块类型
    std::string rptId;             // 报告 ID
    std::string dataSetRef;        // 数据集引用
    uint32_t confRev;            // 配置版本
    uint8_t trgOps;              // 触发选项
    uint8_t optFlds;             // 可选域
    uint16_t bufTime;             // 缓冲时间 (毫秒)
    uint32_t intgPd;             // 集成周期 (毫秒)
    uint32_t bufSize;             // 缓冲区大小
};

/**
 * @brief 报告回调函数类型
 */
using ReportCallback = std::function<void(const std::string& rptId, const std::vector<ReportEntry>& entries)>;

/**
 * @brief 报告控制块适配器类
 *
 * 管理 BRCB/URCB 报告控制块，支持数据集变化触发。
 */
class ReportAdapter {
public:
    /**
     * @brief 创建报告控制块适配器
     * @param server libiec61850 服务器
     * @param config 报告控制块配置
     * @return 成功返回指针，失败返回 nullptr
     */
    static ReportAdapter* create(IedServer* server, const ReportControlConfig& config);

    /**
     * @brief 销毁报告控制块适配器
     */
    void destroy();

    /**
     * @brief 检查适配器是否已初始化
     */
    bool isInitialized() const { return server_ != nullptr && rcb_ != nullptr; }

    /**
     * @brief 启用/禁用报告控制块
     * @param enable true 启用, false 禁用
     */
    bool enable(bool enable);

    /**
     * @brief 检查报告控制块是否已启用
     */
    bool isEnabled() const { return enabled_; }

    /**
     * @brief 设置报告触发回调
     * @param callback 报告回调函数
     */
    void setReportCallback(ReportCallback callback) {
        reportCallback_ = callback;
    }

    /**
     * @brief 触发报告
     * @param trigger 触发选项
     * @return 成功返回 true
     */
    bool trigger(uint8_t trigger = ReportTriggerOptions::DATA_CHANGE);

    /**
     * @brief 批量添加报告条目
     * @param entries 报告条目列表
     * @return 成功添加的数量
     */
    int addReportEntries(const std::vector<ReportEntry>& entries);

    /**
     * @brief 添加单个报告条目
     * @param entry 报告条目
     * @return 成功返回 true
     */
    bool addReportEntry(const ReportEntry& entry);

    /**
     * @brief 设置数据集变化监听
     * @param dataChangeListener 数据集变化回调
     */
    void setDataSetChangeListener(std::function<void(const std::string& dataSetRef)> listener);

    /**
     * @brief 获取报告 ID
     */
    const std::string& getRptId() const { return config_.rptId; }

    /**
     * @brief 获取报告类型
     */
    ReportType getType() const { return config_.type; }

private:
    ReportAdapter(IedServer* server, const ReportControlConfig& config);
    ~ReportAdapter();

    // 禁止拷贝
    ReportAdapter(const ReportAdapter&) = delete;
    ReportAdapter& operator=(const ReportAdapter&) = delete;

    /**
     * @brief 初始化报告控制块
     */
    bool initialize();

    /**
     * @brief 清理报告条目
     */
    void cleanupEntries();

private:
    ReportControlConfig config_;      // 配置信息
    IedServer* server_;             // libiec61850 服务器
    ReportControlBlock rcb_;        // 报告控制块句柄
    bool enabled_;                  // 启用标志

    std::vector<ReportEntry> entries_;    // 报告条目缓冲区
    std::mutex entriesMutex_;         // 条目访问锁

    ReportCallback reportCallback_;        // 报告回调
    std::function<void(const std::string&)> dataChangeListener_;  // 数据集变化监听
};

} // namespace IPC

#endif // HAS_LIBIEC61850
#endif // REPORT_ADAPTER_H
