#ifndef DATA_POOL_CLIENT_H
#define DATA_POOL_CLIENT_H

#include "Common.h"
#include "SharedDataPool.h"
#include "IPCEventCenter.h"
#include "SOERecorder.h"
#include "PersistentStorage.h"
#include "VotingEngine.h"
#include "IEC61850Mapper.h"
#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace IPC {

/**
 * @brief 数据池客户端 - 简化接口封装
 * 
 * 提供易用的 API，封装数据池和事件中心的常用操作
 */
class DataPoolClient {
public:
    /**
     * @brief 配置参数
     */
    struct Config {
        std::string poolName;       // 数据池共享内存名称
        std::string eventName;      // 事件中心共享内存名称
        std::string soeName;        // SOE记录器共享内存名称
        std::string processName;    // 进程名称
        uint32_t yxCount;           // YX 点位数量
        uint32_t ycCount;           // YC 点位数量
        uint32_t dzCount;           // DZ 点位数量
        uint32_t ykCount;           // YK 点位数量
        uint32_t eventCapacity;     // 事件缓冲区容量
        uint32_t soeCapacity;       // SOE缓冲区容量
        bool create;                // 是否创建（false 则连接）
        bool enablePersistence;     // 是否启用持久化
        bool enableSOE;             // 是否启用SOE记录
        bool enableVoting;          // 是否启用表决引擎
        bool enableIEC61850;        // 是否启用IEC61850映射
        
        // 持久化配置
        PersistentConfig persistConfig;
        
        // 表决引擎配置
        VotingEngine::ShmConfig votingConfig;
        
        // IEC61850映射器配置
        IEC61850Mapper::Config iec61850Config;
        
        Config()
            : poolName("/ipc_data_pool"),
              eventName("/ipc_events"),
              soeName("/ipc_soe"),
              processName("unnamed"),
              yxCount(1000), ycCount(1000),
              dzCount(100), ykCount(100),
              eventCapacity(10000),
              soeCapacity(100000),
              create(false),
              enablePersistence(true),
              enableSOE(true),
              enableVoting(false),
              enableIEC61850(false) {}
    };
    
    /**
     * @brief 创建或连接数据池
     * @param config 配置参数
     * @return 客户端指针，失败返回 nullptr
     */
    static DataPoolClient* init(const Config& config);
    
    /**
     * @brief 关闭客户端
     */
    void shutdown();
    
    // ========== 数据操作（简化接口）==========
    
    /**
     * @brief 设置 YX 值（遥信）
     */
    bool setYX(uint64_t key, uint8_t value, uint8_t quality = 0);
    
    /**
     * @brief 获取 YX 值
     */
    bool getYX(uint64_t key, uint8_t& value, uint8_t& quality);
    
    /**
     * @brief 设置 YC 值（遥测）
     */
    bool setYC(uint64_t key, float value, uint8_t quality = 0);
    
    /**
     * @brief 获取 YC 值
     */
    bool getYC(uint64_t key, float& value, uint8_t& quality);
    
    /**
     * @brief 设置 DZ 值（定值）
     */
    bool setDZ(uint64_t key, float value, uint8_t quality = 0);
    
    /**
     * @brief 获取 DZ 值
     */
    bool getDZ(uint64_t key, float& value, uint8_t& quality);
    
    /**
     * @brief 设置 YK 值（遥控）
     */
    bool setYK(uint64_t key, uint8_t value, uint8_t quality = 0);
    
    /**
     * @brief 获取 YK 值
     */
    bool getYK(uint64_t key, uint8_t& value, uint8_t& quality);
    
    /**
     * @brief 通过索引直接访问（高性能）
     */
    bool setYXByIndex(uint32_t index, uint8_t value, uint8_t quality = 0);
    bool getYXByIndex(uint32_t index, uint8_t& value, uint8_t& quality);
    bool setYCByIndex(uint32_t index, float value, uint8_t quality = 0);
    bool getYCByIndex(uint32_t index, float& value, uint8_t& quality);
    
    // ========== 点位注册 ==========
    
    /**
     * @brief 注册数据点
     */
    bool registerPoint(uint64_t key, PointType type, uint32_t& index);
    
    /**
     * @brief 查找数据点
     */
    bool findPoint(uint64_t key, PointType& type, uint32_t& index);
    
    // ========== 事件操作 ==========
    
    /**
     * @brief 发布数据变更事件
     */
    bool publishEvent(uint64_t key, PointType type, uint32_t oldValue, uint32_t newValue);
    bool publishEvent(uint64_t key, PointType type, float oldValue, float newValue);
    
    /**
     * @brief 订阅事件
     * @param callback 回调函数
     * @return 订阅者ID，失败返回 INVALID_INDEX
     */
    uint32_t subscribe(std::function<void(const Event&)> callback);
    
    /**
     * @brief 取消订阅
     */
    bool unsubscribe(uint32_t subscriberId);
    
    /**
     * @brief 处理待处理事件
     * @param subscriberId 订阅者ID
     * @param maxEvents 最大处理数量（0 表示无限制）
     * @return 处理的事件数量
     */
    uint32_t processEvents(uint32_t subscriberId, uint32_t maxEvents = 0);
    
    /**
     * @brief 拉取单个事件
     */
    bool pollEvent(uint32_t subscriberId, Event& event);
    
    // ========== 便捷方法：设置值并发布事件 ==========
    
    /**
     * @brief 设置 YX 值并发布事件
     */
    bool setYXWithEvent(uint64_t key, uint8_t value, uint8_t quality = 0);
    
    /**
     * @brief 设置 YC 值并发布事件
     */
    bool setYCWithEvent(uint64_t key, float value, uint8_t quality = 0);
    
    // ========== 状态查询 ==========
    
    bool isValid() const;
    bool isCreator() const { return m_isCreator; }
    const std::string& getProcessName() const { return m_processName; }
    uint32_t getProcessId() const { return m_processId; }
    
    SharedDataPool* getDataPool() { return m_dataPool; }
    IPCEventCenter* getEventCenter() { return m_eventCenter; }
    
    // ========== 进程管理 ==========
    
    /**
     * @brief 更新心跳
     */
    void updateHeartbeat();
    
    /**
     * @brief 检查进程健康状态
     */
    ProcessHealth checkProcessHealth(uint32_t processId);
    
    /**
     * @brief 获取活跃进程列表
     */
    uint32_t getActiveProcessList(uint32_t* processIds, uint32_t maxCount);
    
    /**
     * @brief 清理死亡进程
     */
    uint32_t cleanupDeadProcesses();
    
    // ========== 心跳与健康监控 ==========
    
    /**
     * @brief 启动心跳线程
     * @param intervalMs 心跳间隔（毫秒），默认1000ms
     */
    void startHeartbeat(uint32_t intervalMs = HEARTBEAT_INTERVAL_MS);
    
    /**
     * @brief 停止心跳线程
     */
    void stopHeartbeat();
    
    /**
     * @brief 启动健康监控线程
     * @param checkIntervalMs 检查间隔（毫秒），默认2000ms
     * @param callback 进程状态变化回调（可选）
     */
    void startHealthMonitor(uint32_t checkIntervalMs = HEALTH_CHECK_INTERVAL_MS,
                            std::function<void(uint32_t, ProcessHealth, ProcessHealth)> callback = nullptr);
    
    /**
     * @brief 停止健康监控线程
     */
    void stopHealthMonitor();
    
    /**
     * @brief 设置进程死亡回调
     */
    void setProcessDeathCallback(std::function<void(uint32_t, pid_t, const char*)> callback);
    
    /**
     * @brief 是否启用心跳
     */
    bool isHeartbeatRunning() const;
    
    /**
     * @brief 是否启用健康监控
     */
    bool isHealthMonitorRunning() const;
    
    // ========== 统计信息 ==========
    
    /**
     * @brief 获取统计信息
     */
    DataPoolStats getStats() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    // ========== 快照持久化 ==========
    
    /**
     * @brief 保存快照到文件
     */
    bool saveSnapshot(const char* filename);
    
    /**
     * @brief 从文件加载快照
     */
    bool loadSnapshot(const char* filename);
    
    /**
     * @brief 验证快照文件
     */
    bool validateSnapshot(const char* filename);
    
    // ========== SOE 事件记录 ==========
    
    /**
     * @brief 记录SOE事件
     */
    bool recordSOE(const SOERecord& record);
    
    /**
     * @brief 记录遥信变位SOE
     */
    bool recordSOEYXChange(uint32_t pointKey, uint8_t oldValue, uint8_t newValue, 
                           uint8_t priority = 128);
    
    /**
     * @brief 记录遥控执行SOE
     */
    bool recordSOEYKExecute(uint32_t pointKey, uint8_t command, 
                            uint8_t priority = 200);
    
    /**
     * @brief 记录保护动作SOE
     */
    bool recordSOEProtectionAct(uint32_t pointKey, uint8_t action,
                                uint8_t priority = 255);
    
    /**
     * @brief 查询SOE记录
     */
    bool querySOE(const SOEQueryCondition& condition,
                  SOERecord* records, uint32_t& count, uint32_t maxCount);
    
    /**
     * @brief 获取最新SOE记录
     */
    bool getLatestSOE(uint32_t count, SOERecord* records, uint32_t& actualCount);
    
    /**
     * @brief 获取SOE统计信息
     */
    SOEStats getSOEStats() const;
    
    /**
     * @brief 导出SOE到CSV
     */
    bool exportSOEToCSV(const char* filename, 
                        const SOEQueryCondition* condition = nullptr);
    
    // ========== 持久化管理 ==========
    
    /**
     * @brief 初始化数据（从快照或默认值）
     */
    bool initializeData();
    
    /**
     * @brief 从快照恢复
     */
    bool restoreFromSnapshot(const char* filename = nullptr);
    
    /**
     * @brief 启用自动快照
     */
    void enableAutoSnapshot(bool enable);
    
    /**
     * @brief 设置快照间隔
     */
    void setSnapshotInterval(uint32_t intervalMs);
    
    /**
     * @brief 手动触发快照
     */
    bool triggerSnapshot();
    
    /**
     * @brief 创建备份
     */
    bool createBackup();
    
    /**
     * @brief 从备份恢复
     */
    bool restoreFromBackup(uint32_t backupIndex = 0);
    
    /**
     * @brief 获取备份列表
     */
    std::vector<std::string> getBackupList() const;
    
    /**
     * @brief 检查是否有有效快照
     */
    bool hasValidSnapshot() const;
    
    /**
     * @brief 获取持久化存储对象
     */
    PersistentStorage* getPersistentStorage() { return m_persistentStorage.get(); }
    
    /**
     * @brief 获取SOE记录器对象
     */
    SOERecorder* getSOERecorder() { return m_soeRecorder; }

    // ========== 三取二表决 ==========

    /**
     * @brief 获取表决引擎
     */
    VotingEngine* getVotingEngine() { return m_votingEngine; }

    /**
     * @brief 添加表决组
     */
    uint32_t addVotingGroup(const VotingConfig& config);

    /**
     * @brief 执行YX表决（自动从数据池读取三源数据）
     */
    bool performVotingYX(uint32_t groupId, VotingOutput& output);

    /**
     * @brief 执行YC表决（自动从数据池读取三源数据）
     */
    bool performVotingYC(uint32_t groupId, VotingOutput& output);

    /**
     * @brief 设置表决告警回调
     */
    void setVotingAlarmCallback(VotingEngine::AlarmCallback callback);

    // ========== IEC 61850 映射 ==========

    /**
     * @brief 获取IEC61850映射器
     */
    IEC61850Mapper* getIEC61850Mapper() { return m_iec61850Mapper; }

    /**
     * @brief 添加IEC61850数据属性映射
     */
    uint32_t addIEC61850Mapping(const DAMapping& mapping);

    /**
     * @brief 从SCL文件加载IEC61850配置
     */
    bool loadSCLConfig(const char* sclFile);

    /**
     * @brief 导出IEC61850配置到SCL文件
     */
    bool exportSCLConfig(const char* sclFile);

    /**
     * @brief 同步数据到IEC61850映射（将数据池数据同步到映射表）
     */
    void syncToIEC61850();

    /**
     * @brief 从IEC61850映射同步数据（从映射表写入数据池）
     */
    void syncFromIEC61850();

private:
    DataPoolClient() = default;
    
    std::string m_processName;
    uint32_t m_processId;
    bool m_isCreator;
    
    SharedDataPool* m_dataPool;
    IPCEventCenter* m_eventCenter;
    SOERecorder* m_soeRecorder;
    std::unique_ptr<PersistentStorage> m_persistentStorage;
    VotingEngine* m_votingEngine;
    IEC61850Mapper* m_iec61850Mapper;
    
    // 心跳线程
    std::atomic<bool> m_heartbeatRunning{false};
    std::thread m_heartbeatThread;
    uint32_t m_heartbeatIntervalMs{HEARTBEAT_INTERVAL_MS};
    
    // 健康监控线程
    std::atomic<bool> m_healthMonitorRunning{false};
    std::thread m_healthMonitorThread;
    uint32_t m_healthCheckIntervalMs{HEALTH_CHECK_INTERVAL_MS};
    std::function<void(uint32_t, ProcessHealth, ProcessHealth)> m_healthChangeCallback;
    std::function<void(uint32_t, pid_t, const char*)> m_processDeathCallback;
    
    // 进程健康状态缓存
    ProcessHealth m_processHealthCache[MAX_PROCESS_COUNT];
    
    // 内部方法
    void heartbeatThreadFunc();
    void healthMonitorThreadFunc();
    
public:
    ~DataPoolClient() = default;
};

} // namespace IPC

#endif // DATA_POOL_CLIENT_H
