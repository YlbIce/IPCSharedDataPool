#ifndef IPC_PERSISTENT_STORAGE_H
#define IPC_PERSISTENT_STORAGE_H

#include "Common.h"
#include "SharedDataPool.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace IPC {

// ========== 持久化常量 ==========

constexpr uint32_t PERSIST_MAGIC = 0x50455253;  // "PERS"
constexpr uint32_t PERSIST_VERSION = 1;
constexpr uint32_t DEFAULT_SNAPSHOT_INTERVAL_MS = 5000;  // 默认快照间隔 5秒
constexpr uint32_t MAX_PERSIST_PATH_LEN = 256;

// ========== 初始化模式 ==========

enum class InitMode : uint8_t {
    LOAD_LAST_VALUE = 0,    ///< 加载上次保存的值
    LOAD_DEFAULT = 1,       ///< 加载默认值
    WAIT_FOR_FRESH = 2,     ///< 等待新数据（标记为无效直到收到更新）
    INVALIDATE = 3          ///< 置无效值
};

// ========== 点位初始化配置 ==========

struct PointInitConfig {
    uint64_t key;           ///< 点位key
    PointType type;         ///< 点位类型
    InitMode initMode;      ///< 初始化模式
    
    union {
        float defaultFloat; ///< YC/DZ 默认值
        uint32_t defaultInt;///< YX/YK 默认值
    } defaultValue;
    
    uint8_t defaultQuality; ///< 默认质量码
    
    PointInitConfig() : key(0), type(PointType::YX), initMode(InitMode::INVALIDATE),
                        defaultQuality(0) {
        defaultValue.defaultInt = 0;
    }
};

// ========== 持久化配置 ==========

struct PersistentConfig {
    // 初始化模式（按类型设置）
    InitMode yxInitMode;        ///< 遥信初始化模式（断路器位置必须 LOAD_LAST_VALUE）
    InitMode ycInitMode;        ///< 遥测初始化模式
    InitMode dzInitMode;        ///< 定值初始化模式
    InitMode ykInitMode;        ///< 遥控初始化模式
    
    // 快照配置
    uint32_t snapshotIntervalMs;    ///< 自动快照间隔（毫秒）
    uint32_t maxSnapshotFiles;      ///< 最大快照文件数（用于循环覆盖）
    bool enableAutoSnapshot;        ///< 是否启用自动快照
    bool enableCompress;            ///< 是否启用压缩（未来扩展）
    
    // 文件路径
    char snapshotPath[MAX_PERSIST_PATH_LEN];  ///< 快照文件路径
    char backupPath[MAX_PERSIST_PATH_LEN];    ///< 备份文件路径
    
    PersistentConfig() 
        : yxInitMode(InitMode::LOAD_LAST_VALUE),  // 断路器位置必须保持
          ycInitMode(InitMode::LOAD_DEFAULT),
          dzInitMode(InitMode::LOAD_LAST_VALUE),  // 定值必须保持
          ykInitMode(InitMode::INVALIDATE),       // 遥控状态不保持
          snapshotIntervalMs(DEFAULT_SNAPSHOT_INTERVAL_MS),
          maxSnapshotFiles(10),
          enableAutoSnapshot(true),
          enableCompress(false) {
        std::strcpy(snapshotPath, "/tmp/ipc_datapool.snapshot");
        std::strcpy(backupPath, "/tmp/ipc_datapool.backup");
    }
};

// ========== 快照文件头部 ==========

#pragma pack(push, 1)
struct PersistHeader {
    uint32_t magic;             ///< 魔数 "PERS"
    uint32_t version;           ///< 版本号
    uint64_t snapshotTime;      ///< 快照时间（纳秒）
    uint64_t shmSize;           ///< 共享内存大小
    uint32_t yxCount;           ///< YX点数
    uint32_t ycCount;           ///< YC点数
    uint32_t dzCount;           ///< DZ点数
    uint32_t ykCount;           ///< YK点数
    uint32_t indexCount;        ///< 索引数
    uint32_t checksum;          ///< 校验和
    uint8_t reserved[80];       ///< 保留对齐到128字节
};
#pragma pack(pop)

static_assert(sizeof(PersistHeader) == 128, "PersistHeader size must be 128 bytes");

// ========== 持久化存储管理器 ==========

/**
 * @brief 持久化存储管理器
 * 
 * 功能：
 * - 掉电保持：自动保存共享内存数据到文件
 * - 启动恢复：按配置模式初始化数据
 * - 多版本备份：循环备份防止数据丢失
 * - 故障恢复：检测并恢复损坏的快照文件
 */
class PersistentStorage {
public:
    /**
     * @brief 创建持久化存储管理器
     * @param dataPool 关联的数据池
     * @param config 持久化配置
     */
    PersistentStorage(SharedDataPool* dataPool, const PersistentConfig& config = PersistentConfig());
    
    ~PersistentStorage();
    
    // ========== 初始化与恢复 ==========
    
    /**
     * @brief 初始化数据池（根据配置模式）
     * @return Result::OK 成功
     * 
     * 根据配置的初始化模式处理：
     * - LOAD_LAST_VALUE: 从快照文件加载
     * - LOAD_DEFAULT: 设置默认值
     * - WAIT_FOR_FRESH: 标记为无效，等待新数据
     * - INVALIDATE: 设置无效质量码
     */
    Result initialize();
    
    /**
     * @brief 从快照文件恢复
     * @param filename 快照文件路径（nullptr使用配置路径）
     * @return Result::OK 成功
     */
    Result restore(const char* filename = nullptr);
    
    /**
     * @brief 恢复指定类型的默认值
     */
    Result restoreDefaults(PointType type);
    
    // ========== 快照操作 ==========
    
    /**
     * @brief 保存当前快照
     * @param filename 快照文件路径（nullptr使用配置路径）
     * @return Result::OK 成功
     */
    Result saveSnapshot(const char* filename = nullptr);
    
    /**
     * @brief 启用自动快照
     */
    void enableAutoSnapshot(bool enable);
    
    /**
     * @brief 设置快照间隔
     * @param intervalMs 间隔（毫秒）
     */
    void setSnapshotInterval(uint32_t intervalMs);
    
    /**
     * @brief 手动触发快照
     */
    Result triggerSnapshot();
    
    // ========== 备份管理 ==========
    
    /**
     * @brief 创建备份
     * @return Result::OK 成功
     */
    Result createBackup();
    
    /**
     * @brief 从备份恢复
     * @param backupIndex 备份索引（0为最新）
     * @return Result::OK 成功
     */
    Result restoreFromBackup(uint32_t backupIndex = 0);
    
    /**
     * @brief 获取备份列表
     */
    std::vector<std::string> getBackupList() const;
    
    /**
     * @brief 清理旧备份
     * @param keepCount 保留数量
     */
    void cleanupOldBackups(uint32_t keepCount);
    
    // ========== 状态查询 ==========
    
    /**
     * @brief 检查快照文件是否存在
     */
    bool hasValidSnapshot() const;
    
    /**
     * @brief 获取快照文件信息
     */
    bool getSnapshotInfo(const char* filename, PersistHeader& header) const;
    
    /**
     * @brief 获取配置
     */
    const PersistentConfig& getConfig() const { return m_config; }
    
    /**
     * @brief 更新配置
     */
    void updateConfig(const PersistentConfig& config);
    
    /**
     * @brief 获取自动快照状态
     */
    bool isAutoSnapshotEnabled() const { return m_autoSnapshot.load(); }
    
    /**
     * @brief 获取上次快照时间
     */
    uint64_t getLastSnapshotTime() const { return m_lastSnapshotTime.load(); }
    
    // ========== 点位初始化配置 ==========
    
    /**
     * @brief 设置点位初始化配置
     */
    void setPointInitConfig(const PointInitConfig& config);
    
    /**
     * @brief 批量设置点位初始化配置
     */
    void setPointInitConfigs(const PointInitConfig* configs, uint32_t count);
    
    /**
     * @brief 获取点位初始化配置
     */
    bool getPointInitConfig(uint64_t key, PointInitConfig& config) const;
    
private:
    // 内部方法
    void snapshotThread();
    Result writeSnapshotFile(const char* filename);
    Result readSnapshotFile(const char* filename);
    std::string generateBackupFilename();
    uint32_t calculateChecksum(const void* data, size_t len) const;
    
    // 按类型初始化
    Result initializeYX();
    Result initializeYC();
    Result initializeDZ();
    Result initializeYK();
    
private:
    SharedDataPool* m_dataPool;         ///< 关联的数据池
    PersistentConfig m_config;          ///< 配置
    
    std::atomic<bool> m_autoSnapshot;   ///< 自动快照开关
    std::atomic<uint64_t> m_lastSnapshotTime; ///< 上次快照时间
    
    std::thread m_snapshotThread;       ///< 快照线程
    std::atomic<bool> m_running;        ///< 运行标志
    std::mutex m_mutex;                 ///< 互斥锁
    std::condition_variable m_cv;       ///< 条件变量
    
    std::vector<PointInitConfig> m_pointConfigs; ///< 点位初始化配置
};

// ========== 辅助函数 ==========

/**
 * @brief 获取默认快照路径
 */
inline std::string getDefaultSnapshotPath(const char* poolName) {
    return std::string("/tmp/") + poolName + ".snapshot";
}

/**
 * @brief 获取默认备份路径
 */
inline std::string getDefaultBackupPath(const char* poolName) {
    return std::string("/tmp/") + poolName + ".backup";
}

} // namespace IPC

#endif // IPC_PERSISTENT_STORAGE_H
