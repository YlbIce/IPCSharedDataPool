/**
 * @file GooseAdapter.h
 * @brief GOOSE 发布器 C++ 适配器
 *
 * 基于 libiec61850 的 GoosePublisher 实现的 C++ 封装，
 * 提供面向对象的 API 接口。
 */

#ifndef GOOSE_ADAPTER_H
#define GOOSE_ADAPTER_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <goose_publisher.h>
#include <linked_list.h>
#include <mms_value.h>

namespace IPC {

/**
 * @brief GOOSE 通信参数配置
 */
struct GooseCommConfig {
    std::string interfaceName;      // 网络接口名 (eth0)
    uint8_t vlanPriority;           // VLAN 优先级 (0-7)
    uint16_t vlanId;               // VLAN ID
    uint16_t appId;                // 应用标识
    uint8_t dstAddress[6];         // 目标 MAC 地址
    bool useVlanTag;             // 是否使用 VLAN 标签

    // GOOSE 控制块信息
    std::string goID;             // GOOSE 标识符
    std::string goCBRef;           // GOOSE 控制块引用
    std::string dataSetRef;        // 数据集引用
    uint32_t confRev;            // 配置版本
    uint32_t timeAllowedToLive;   // 存活时间 (ms)
};

/**
 * @brief GOOSE 数据集成员类型
 */
enum class GooseDataType {
    BOOLEAN,
    INTEGER,
    FLOAT,
    BITSTRING
};

/**
 * @brief GOOSE 数据集成员定义
 */
struct GooseDataSetMember {
    std::string name;             // 成员名称
    GooseDataType type;          // 数据类型
    int index;                   // 在数据集中的索引
    MmsValue* value;            // 值指针 (由 GooseAdapter 管理)
};

/**
 * @brief GOOSE 数据回调函数类型
 */
using GooseDataCallback = std::function<void(const std::string& goCBRef)>;

/**
 * @brief GOOSE 发布器适配器类
 *
 * 提供对 libiec61850 GoosePublisher 的 C++ 封装，
 * 支持动态数据集管理和自动状态更新。
 */
class GooseAdapter {
public:
    /**
     * @brief 创建 GOOSE 发布器适配器
     * @param config GOOSE 通信配置
     * @return 成功返回指针，失败返回 nullptr
     */
    static GooseAdapter* create(const GooseCommConfig& config);

    /**
     * @brief 销毁 GOOSE 发布器
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
     * @brief 添加数据集成员
     * @param member 数据集成员定义
     * @return 成功返回成员索引，失败返回 -1
     */
    int addDataSetMember(const GooseDataSetMember& member);

    /**
     * @brief 批量添加数据集成员
     * @param members 数据集成员列表
     * @return 成功添加的数量
     */
    int addDataSetMembers(const std::vector<GooseDataSetMember>& members);

    /**
     * @brief 更新数据集成员的值
     * @param index 成员索引
     * @param value 新值 (GooseAdapter 会接管此指针)
     */
    void updateMemberValue(int index, MmsValue* value);

    /**
     * @brief 发布 GOOSE 报文
     * @param increaseStNum 是否增加状态号 (默认 true)
     * @return 成功返回 true
     */
    bool publish(bool increaseStNum = true);

    /**
     * @brief 手动增加状态号 (当数据集内容变化时调用)
     * @return 新状态号
     */
    uint64_t increaseStNum();

    /**
     * @brief 获取当前状态号
     */
    uint32_t getStNum() const { return stNum_; }

    /**
     * @brief 获取当前序列号
     */
    uint32_t getSqNum() const { return sqNum_; }

    /**
     * @brief 设置状态号 (仅用于测试)
     */
    void setStNum(uint32_t stNum);

    /**
     * @brief 设置序列号 (仅用于测试)
     */
    void setSqNum(uint32_t sqNum);

    /**
     * @brief 重置状态号和序列号
     */
    void reset();

    /**
     * @brief 获取数据集成员数量
     */
    size_t getMemberCount() const { return members_.size(); }

    /**
     * @brief 获取指定索引的成员
     * @param index 成员索引
     * @return 成员指针，索引无效返回 nullptr
     */
    const GooseDataSetMember* getMember(int index) const;

    /**
     * @brief 设置数据变化回调
     * @param callback 发布前调用的回调
     */
    void setDataChangeCallback(GooseDataCallback callback) {
        dataChangeCallback_ = callback;
    }

    /**
     * @brief 获取 GOOSE 标识符
     */
    const std::string& getGoID() const { return config_.goID; }

    /**
     * @brief 获取 GOOSE 控制块引用
     */
    const std::string& getGoCBRef() const { return config_.goCBRef; }

    /**
     * @brief 获取数据集引用
     */
    const std::string& getDataSetRef() const { return config_.dataSetRef; }

private:
    GooseAdapter(const GooseCommConfig& config);
    ~GooseAdapter();

    // 禁止拷贝
    GooseAdapter(const GooseAdapter&) = delete;
    GooseAdapter& operator=(const GooseAdapter&) = delete;

    /**
     * @brief 初始化 GOOSE 发布器
     */
    bool initialize();

    /**
     * @brief 销毁 MmsValue 资源
     */
    void cleanupValues();

private:
    GooseCommConfig config_;           // 配置信息
    GoosePublisher publisher_;          // libiec61850 发布器
    LinkedList dataSet_;              // 数据集链表
    std::vector<GooseDataSetMember> members_;  // 成员信息
    std::vector<MmsValue*> values_;           // 值指针

    uint32_t stNum_;                   // 状态号
    uint32_t sqNum_;                   // 序列号
    bool enabled_;                     // 启用标志

    std::mutex valuesMutex_;            // 值访问锁
    GooseDataCallback dataChangeCallback_;  // 数据变化回调
};

} // namespace IPC

#endif // HAS_LIBIEC61850
#endif // GOOSE_ADAPTER_H
