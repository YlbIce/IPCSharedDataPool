#ifndef VOTING_ENGINE_H
#define VOTING_ENGINE_H

#include "Common.h"
#include <cstdint>
#include <cstring>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace IPC {

/**
 * @brief 表决源状态
 */
enum class SourceStatus : uint8_t {
    VALID = 0,          // 有效
    INVALID = 1,        // 无效（质量码坏）
    TIMEOUT = 2,        // 超时
    OUT_OF_RANGE = 3,   // 超出范围
    DISCONNECTED = 4    // 断开
};

/**
 * @brief 表决结果类型
 */
enum class VotingResult : uint8_t {
    UNANIMOUS = 0,      // 一致（三源相同）
    MAJORITY = 1,       // 多数（二取一）
    DISAGREE = 2,       // 不一致（三源各不相同）
    INSUFFICIENT = 3,   // 有效源不足
    FAILED = 4          // 表决失败
};

/**
 * @brief 偏差告警级别
 */
enum class DeviationLevel : uint8_t {
    NONE = 0,           // 无偏差
    MINOR = 1,          // 轻微偏差
    MODERATE = 2,       // 中等偏差
    SEVERE = 3          // 严重偏差
};

/**
 * @brief 表决配置（每个表决组）
 */
#pragma pack(push, 1)
struct VotingConfig {
    uint32_t groupId;           // 表决组ID
    char name[32];              // 表决组名称
    uint64_t sourceKeyA;        // 源A的key
    uint64_t sourceKeyB;        // 源B的key
    uint64_t sourceKeyC;        // 源C的key
    uint8_t sourceType;         // 源类型（0=YX, 1=YC）
    uint8_t votingStrategy;     // 表决策略（0=严格三取二, 1=宽松, 2=优先级）
    uint8_t prioritySource;     // 优先源（策略2时有效）0=A, 1=B, 2=C
    uint8_t enableDeviation;    // 是否启用偏差检测
    float deviationLimit;       // 偏差限值（YC用）
    uint8_t deviationCountLimit;// 偏差计数限值
    uint32_t timeoutMs;         // 超时时间（毫秒）
    uint32_t alarmMask;         // 告警屏蔽字
    uint8_t reserved[67];       // 保留
};
#pragma pack(pop)

static_assert(sizeof(VotingConfig) == 144, "VotingConfig size mismatch");

/**
 * @brief 表决源数据
 */
#pragma pack(push, 1)
struct SourceData {
    union {
        uint8_t yxValue;        // YX值
        float ycValue;          // YC值
        uint32_t rawValue;      // 原始值
    };
    uint8_t quality;            // 质量码
    uint8_t status;             // SourceStatus
    uint8_t reserved[2];        // 保留对齐
    uint64_t timestamp;         // 时间戳
};
#pragma pack(pop)

static_assert(sizeof(SourceData) == 16, "SourceData size mismatch");

/**
 * @brief 表决结果
 */
#pragma pack(push, 1)
struct VotingOutput {
    uint32_t groupId;           // 表决组ID
    uint8_t result;             // VotingResult
    uint8_t deviationLevel;     // DeviationLevel
    uint8_t validSourceCount;   // 有效源数量
    uint8_t reserved;
    
    union {
        uint8_t yxValue;        // 表决后的YX值
        float ycValue;          // 表决后的YC值
        uint32_t rawValue;
    };
    
    uint8_t quality;            // 输出质量码
    uint8_t deviationCount;     // 连续偏差计数
    uint16_t alarmFlags;        // 告警标志
    uint64_t timestamp;         // 表决时间戳
    
    // 三个源的原始数据
    SourceData sources[3];
    
    uint8_t padding[56];        // 填充到128字节
};
#pragma pack(pop)

static_assert(sizeof(VotingOutput) == 128, "VotingOutput size mismatch");

/**
 * @brief 表决组统计
 */
struct VotingStats {
    uint32_t totalVotes;        // 总表决次数
    uint32_t unanimousCount;    // 一致次数
    uint32_t majorityCount;     // 多数次数
    uint32_t disagreeCount;     // 不一致次数
    uint32_t insufficientCount;// 有效源不足次数
    uint32_t deviationAlarms;   // 偏差告警次数
    uint64_t lastVotingTime;    // 最后表决时间
};

/**
 * @brief 表决引擎 - 三取二表决机制
 * 
 * 用于电力保护系统的三取二表决，支持：
 * - 严格三取二表决
 * - 宽松表决（任意二源一致即可）
 * - 优先级表决（指定源优先）
 * - 偏差检测与告警
 */
class VotingEngine {
public:
    /**
     * @brief 告警回调类型
     */
    using AlarmCallback = std::function<void(uint32_t groupId, DeviationLevel level, 
                                              const char* message)>;
    
    /**
     * @brief 配置共享内存参数
     */
    struct ShmConfig {
        std::string shmName;    // 共享内存名称
        uint32_t maxGroups;     // 最大表决组数
        bool create;            // 是否创建
        
        ShmConfig()
            : shmName("/ipc_voting"),
              maxGroups(1000),
              create(false) {}
    };
    
    /**
     * @brief 创建或连接表决引擎
     * @param config 共享内存配置
     * @return 表决引擎指针，失败返回 nullptr
     */
    static VotingEngine* create(const ShmConfig& config);
    
    /**
     * @brief 销毁表决引擎
     */
    void destroy();
    
    // ========== 表决组管理 ==========
    
    /**
     * @brief 添加表决组
     * @param config 表决组配置
     * @return 成功返回组ID，失败返回 INVALID_INDEX
     */
    uint32_t addVotingGroup(const VotingConfig& config);
    
    /**
     * @brief 删除表决组
     */
    bool removeVotingGroup(uint32_t groupId);
    
    /**
     * @brief 更新表决组配置
     */
    bool updateVotingGroup(uint32_t groupId, const VotingConfig& config);
    
    /**
     * @brief 获取表决组配置
     */
    bool getVotingGroupConfig(uint32_t groupId, VotingConfig& config);
    
    /**
     * @brief 获取表决组数量
     */
    uint32_t getVotingGroupCount();
    
    // ========== 表决操作 ==========
    
    /**
     * @brief 执行表决（YX）
     * @param groupId 表决组ID
     * @param sources 三个源的数据
     * @param output 输出结果
     * @return 成功返回true
     */
    bool voteYX(uint32_t groupId, const SourceData sources[3], VotingOutput& output);
    
    /**
     * @brief 执行表决（YC）
     * @param groupId 表决组ID
     * @param sources 三个源的数据
     * @param output 输出结果
     * @return 成功返回true
     */
    bool voteYC(uint32_t groupId, const SourceData sources[3], VotingOutput& output);
    
    /**
     * @brief 快速表决（无组配置，使用默认策略）
     */
    static VotingResult quickVoteYX(const SourceData sources[3], uint8_t& resultValue,
                                     uint8_t strategy = 0);
    
    /**
     * @brief 快速表决（YC，带偏差检测）
     */
    static VotingResult quickVoteYC(const SourceData sources[3], float& resultValue,
                                     float deviationLimit = 0.0f, uint8_t strategy = 0);
    
    // ========== 结果查询 ==========
    
    /**
     * @brief 获取表决结果
     */
    bool getVotingOutput(uint32_t groupId, VotingOutput& output);
    
    /**
     * @brief 获取表决统计
     */
    bool getVotingStats(uint32_t groupId, VotingStats& stats);
    
    /**
     * @brief 重置统计
     */
    bool resetVotingStats(uint32_t groupId);
    
    // ========== 告警管理 ==========
    
    /**
     * @brief 设置告警回调
     */
    void setAlarmCallback(AlarmCallback callback);
    
    /**
     * @brief 确认告警
     */
    bool acknowledgeAlarm(uint32_t groupId);
    
    /**
     * @brief 获取活跃告警数量
     */
    uint32_t getActiveAlarmCount();
    
    /**
     * @brief 获取活跃告警列表
     */
    uint32_t getActiveAlarms(uint32_t* groupIds, DeviationLevel* levels, 
                              uint32_t maxCount);
    
    // ========== 健康检测 ==========
    
    /**
     * @brief 检查表决组健康状态
     */
    bool checkHealth(uint32_t groupId, uint8_t& healthStatus, char* message = nullptr);
    
    /**
     * @brief 获取有效源数量
     */
    uint8_t getValidSourceCount(uint32_t groupId);
    
private:
    VotingEngine() = default;
    
    // 内部方法
    VotingResult performVotingYX(const SourceData sources[3], 
                                  const VotingConfig& config, VotingOutput& output);
    VotingResult performVotingYC(const SourceData sources[3], 
                                  const VotingConfig& config, VotingOutput& output);
    void checkDeviationYX(const SourceData sources[3], const VotingConfig& config,
                           VotingOutput& output);
    void checkDeviationYC(const SourceData sources[3], const VotingConfig& config,
                           VotingOutput& output);
    void triggerAlarm(uint32_t groupId, DeviationLevel level, const char* message);
    
public:
    // 共享内存布局
    struct ShmHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t maxGroups;
        uint32_t groupCount;
        std::atomic<uint32_t> activeAlarmCount;
        uint64_t createTime;
        uint8_t reserved[64];
    };
    
private:
    void* m_shm;
    void* m_shmData;
    size_t m_shmSize;
    bool m_isCreator;
    
    AlarmCallback m_alarmCallback;
    
    // 配置和输出区域指针
    VotingConfig* m_configs;
    VotingOutput* m_outputs;
    VotingStats* m_stats;
    uint8_t* m_healthFlags;
    uint16_t* m_alarmFlags;
};

} // namespace IPC

#endif // VOTING_ENGINE_H
