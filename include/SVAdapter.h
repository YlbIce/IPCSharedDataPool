/**
 * @file SVAdapter.h
 * @brief SV (Sampled Values) 发布器 C++ 适配器
 *
 * 基于 libiec61850 的 SVPublisher 实现的 C++ 封装，
 * 提供面向对象的 API 接口。
 */

#ifndef SV_ADAPTER_H
#define SV_ADAPTER_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <sv_publisher.h>
#include <iec61850_common.h>

namespace IPC {

/**
 * @brief SV 通信参数配置
 */
struct SVCommConfig {
    std::string interfaceName;      // 网络接口名 (eth0)
    uint8_t vlanPriority;           // VLAN 优先级 (0-7)
    uint16_t vlanId;               // VLAN ID
    uint16_t appId;                // 应用标识
    uint8_t dstAddress[6];         // 目标 MAC 地址
    bool useVlanTag;             // 是否使用 VLAN 标签

    // SV 控制块信息
    std::string svID;              // SV 标识符
    std::string datset;            // 数据集引用
    uint16_t smpRate;             // 采样率 (每周期采样数或每秒采样数)
    uint8_t smpMod;              // 采样模式
    uint32_t confRev;            // 配置版本
};

/**
 * @brief SV 采样值数据类型
 */
enum class SVDataType {
    INT8,
    INT32,
    INT64,
    FLOAT,
    FLOAT64,
    TIMESTAMP,
    QUALITY
};

/**
 * @brief SV ASDU 成员定义
 */
struct SVASDUMember {
    std::string name;             // 成员名称
    SVDataType type;            // 数据类型
    int index;                   // 在 ASDU 中的索引
    int bufferOffset;             // 在 ASDU 缓冲区中的字节偏移
};

/**
 * @brief SV 发布回调函数类型
 */
using SVPublishCallback = std::function<void(const std::string& svID, uint32_t smpCnt)>;

/**
 * @brief SV 发布器适配器类
 *
 * 提供对 libiec61850 SVPublisher 的 C++ 封装，
 * 支持多 ASDU 管理和高速采样。
 */
class SVAdapter {
public:
    /**
     * @brief 创建 SV 发布器适配器
     * @param config SV 通信配置
     * @return 成功返回指针，失败返回 nullptr
     */
    static SVAdapter* create(const SVCommConfig& config);

    /**
     * @brief 销毁 SV 发布器
     */
    void destroy();

    /**
     * @brief 检查发布器是否已初始化
     */
    bool isInitialized() const { return publisher_ != nullptr; }

    /**
     * @brief 检查发布器是否已启用
     */
    bool isEnabled() const { return enabled_; }

    /**
     * @brief 启用/禁用发布器
     * @param enable true 启用, false 禁用
     */
    void setEnabled(bool enable) { enabled_ = enable; }

    /**
     * @brief 添加 ASDU (Application Service Data Unit)
     * @param svID SV 标识符
     * @param datset 数据集名称
     * @param confRev 配置版本
     * @return ASDU 指针，失败返回 nullptr
     */
    SVPublisher_ASDU addASDU(const std::string& svID,
                               const std::string& datset,
                               uint32_t confRev = 0);

    /**
     * @brief 添加 ASDU 成员
     * @param asduIndex ASDU 索引
     * @param member ASDU 成员定义
     * @return 成功返回 true
     */
    bool addASDUMember(int asduIndex, const SVASDUMember& member);

    /**
     * @brief 批量添加 ASDU 成员
     * @param asduIndex ASDU 索引
     * @param members ASDU 成员列表
     * @return 成功添加的数量
     */
    int addASDUMembers(int asduIndex, const std::vector<SVASDUMember>& members);

    /**
     * @brief 设置 ASDU 成员值 (INT8)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 值
     */
    void setINT8Value(int asduIndex, int index, int8_t value);

    /**
     * @brief 设置 ASDU 成员值 (INT32)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 值
     */
    void setINT32Value(int asduIndex, int index, int32_t value);

    /**
     * @brief 设置 ASDU 成员值 (INT64)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 值
     */
    void setINT64Value(int asduIndex, int index, int64_t value);

    /**
     * @brief 设置 ASDU 成员值 (FLOAT)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 值
     */
    void setFLOATValue(int asduIndex, int index, float value);

    /**
     * @brief 设置 ASDU 成员值 (FLOAT64)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 值
     */
    void setFLOAT64Value(int asduIndex, int index, double value);

    /**
     * @brief 设置 ASDU 成员值 (Timestamp)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 时间戳
     */
    void setTimestampValue(int asduIndex, int index, Timestamp value);

    /**
     * @brief 设置 ASDU 成员值 (Quality)
     * @param asduIndex ASDU 索引
     * @param index 成员索引
     * @param value 质量值
     */
    void setQualityValue(int asduIndex, int index, Quality value);

    /**
     * @brief 设置采样计数
     * @param asduIndex ASDU 索引
     * @param smpCnt 采样计数
     */
    void setSmpCnt(int asduIndex, uint16_t smpCnt);

    /**
     * @brief 增加采样计数
     * @param asduIndex ASDU 索引
     */
    void increaseSmpCnt(int asduIndex);

    /**
     * @brief 设置刷新时间 (Refresh Time)
     * @param asduIndex ASDU 索引
     * @param refrTm 刷新时间 (毫秒)
     */
    void setRefrTm(int asduIndex, uint64_t refrTm);

    /**
     * @brief 设置刷新时间 (纳秒级)
     * @param asduIndex ASDU 索引
     * @param refrTmNs 刷新时间 (纳秒)
     */
    void setRefrTmNs(int asduIndex, nsSinceEpoch refrTmNs);

    /**
     * @brief 设置采样模式
     * @param asduIndex ASDU 索引
     * @param smpMod 采样模式 (0=per nominal, 1=per second, 2=per sample)
     */
    void setSmpMod(int asduIndex, uint8_t smpMod);

    /**
     * @brief 设置采样率
     * @param asduIndex ASDU 索引
     * @param smpRate 采样率
     */
    void setSmpRate(int asduIndex, uint16_t smpRate);

    /**
     * @brief 设置采样同步标志
     * @param asduIndex ASDU 索引
     * @param smpSynch 同步标志 (0=未同步, 1=局部时钟, 2=全局时钟)
     */
    void setSmpSynch(int asduIndex, uint16_t smpSynch);

    /**
     * @brief 发布所有 ASDU 的采样值
     * @return 成功返回 true
     */
    bool publish();

    /**
     * @brief 完成发布器设置
     *
     * 必须在调用 publish() 之前调用
     */
    void setupComplete();

    /**
     * @brief 重置 ASDU 缓冲区
     * @param asduIndex ASDU 索引
     */
    void resetASDUBuffer(int asduIndex);

    /**
     * @brief 获取 ASDU 数量
     */
    size_t getASDUCount() const { return asdus_.size(); }

    /**
     * @brief 获取指定 ASDU 的成员数量
     * @param asduIndex ASDU 索引
     * @return 成员数量
     */
    size_t getMemberCount(int asduIndex) const;

    /**
     * @brief 获取采样计数
     * @param asduIndex ASDU 索引
     * @return 采样计数
     */
    uint16_t getSmpCnt(int asduIndex) const;

    /**
     * @brief 设置发布回调
     * @param callback 发布前调用的回调
     */
    void setPublishCallback(SVPublishCallback callback) {
        publishCallback_ = callback;
    }

    /**
     * @brief 获取 SV 标识符
     */
    const std::string& getSVId() const { return config_.svID; }

private:
    SVAdapter(const SVCommConfig& config);
    ~SVAdapter();

    // 禁止拷贝
    SVAdapter(const SVAdapter&) = delete;
    SVAdapter& operator=(const SVAdapter&) = delete;

    /**
     * @brief 初始化 SV 发布器
     */
    bool initialize();

    /**
     * @brief 清理 ASDU 资源
     */
    void cleanupASDUs();

private:
    SVCommConfig config_;                        // 配置信息
    SVPublisher publisher_;                       // libiec61850 发布器
    std::vector<SVPublisher_ASDU> asdus_;     // ASDU 列表
    std::vector<std::vector<SVASDUMember>> members_;  // ASDU 成员信息

    bool enabled_;                                // 启用标志
    bool setupComplete_;                          // 设置完成标志

    std::mutex asduMutex_;                         // ASDU 访问锁

    SVPublishCallback publishCallback_;              // 发布回调
};

} // namespace IPC

#endif // HAS_LIBIEC61850
#endif // SV_ADAPTER_H
