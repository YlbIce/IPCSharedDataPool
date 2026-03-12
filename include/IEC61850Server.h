/**
 * @file IEC61850Server.h
 * @brief IEC61850 MMS服务器封装
 * 
 * 基于libiec61850实现的IEC61850 MMS服务器，提供标准化的IEC61850服务接口
 */

#ifndef IEC61850_SERVER_H
#define IEC61850_SERVER_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <iec61850_server.h>
#include <goose_publisher.h>
#include "GooseAdapter.h"
#include "SVAdapter.h"

namespace IPC {

/**
 * @brief IEC61850数据点绑定信息
 */
struct IEC61850DataBinding {
    std::string iedName;           // IED名称
    std::string logicalDevice;     // 逻辑设备
    std::string logicalNode;       // 逻辑节点
    std::string dataObject;        // 数据对象
    std::string dataAttribute;     // 数据属性
    std::string fc;                // 功能约束 (FC)
    uint32_t poolOffset;           // 共享内存池偏移量
    bool isWritable;               // 是否可写
    
    // 完整引用 "LD/LN.DO.DA"
    std::string getFullRef() const {
        return logicalDevice + "/" + logicalNode + "." + dataObject + "." + dataAttribute;
    }
};

/**
 * @brief IEC61850服务器配置
 */
struct IEC61850ServerConfig {
    int tcpPort = 102;             // MMS TCP端口
    std::string modelName = "model.cfg";  // SCL模型文件
    std::string iedName = "MyIED";        // IED名称
    int maxConnections = 5;        // 最大客户端连接数
    bool enableGoose = false;      // 是否启用GOOSE
    int gooseInterfaceId = 0;     // GOOSE网络接口ID
    
    // 报告控制块配置
    bool enableBRCB = true;       // 缓冲报告控制块
    bool enableURCB = true;       // 非缓冲报告控制块
    int reportBufSize = 100;      // 报告缓冲区大小
};

/**
 * @brief MMS操作回调函数类型
 */
using MMSReadCallback = std::function<bool(const IEC61850DataBinding&, uint8_t*)>;
using MMSWriteCallback = std::function<bool(const IEC61850DataBinding&, const uint8_t*)>;
using ControlCallback = std::function<bool(const std::string&, bool)>;

/**
 * @brief IEC61850 MMS服务器类
 * 
 * 实现标准的IEC61850 MMS服务器，支持：
 * - MMS数据读写服务 (GetDataValue, SetDataValue)
 * - 控制服务 (Operate, Select, Cancel)
 * - 报告服务 (Report Control Block)
 * - GOOSE发布 (可选)
 * - SCL文件解析和模型加载
 */
class IEC61850Server {
public:
    IEC61850Server();
    ~IEC61850Server();
    
    // 禁止拷贝
    IEC61850Server(const IEC61850Server&) = delete;
    IEC61850Server& operator=(const IEC61850Server&) = delete;
    
    /**
     * @brief 初始化服务器
     * @param config 服务器配置
     * @return 成功返回true
     */
    bool initialize(const IEC61850ServerConfig& config);
    
    /**
     * @brief 启动服务器（阻塞模式）
     * @return 成功返回true
     */
    bool run();
    
    /**
     * @brief 启动服务器（非阻塞模式，后台线程）
     * @return 成功返回true
     */
    bool startAsync();
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 设置读取回调
     * @param callback 读取数据回调函数
     */
    void setReadCallback(MMSReadCallback callback);
    
    /**
     * @brief 设置写入回调
     * @param callback 写入数据回调函数
     */
    void setWriteCallback(MMSWriteCallback callback);
    
    /**
     * @brief 设置控制操作回调
     * @param callback 控制操作回调函数
     */
    void setControlCallback(ControlCallback callback);
    
    /**
     * @brief 添加数据点绑定
     * @param binding 数据点绑定信息
     * @return 成功返回true
     */
    bool addDataBinding(const IEC61850DataBinding& binding);
    
    /**
     * @brief 批量添加数据点绑定
     * @param bindings 数据点绑定列表
     * @return 成功数量
     */
    int addDataBindings(const std::vector<IEC61850DataBinding>& bindings);
    
    /**
     * @brief 从SCL文件加载数据点绑定
     * @param sclFile SCL配置文件路径
     * @return 成功数量
     */
    int loadBindingsFromSCL(const std::string& sclFile);
    
    /**
     * @brief 触发数据变化通知（用于报告和GOOSE）
     * @param fullRef 数据点完整引用
     */
    void triggerDataChange(const std::string& fullRef);
    
    /**
     * @brief 发送GOOSE报文
     * @param dataSetId 数据集标识符
     * @param values 数据值数组
     * @param count 数组长度
     * @return 成功返回true
     */
    bool publishGoose(const std::string& dataSetId, const MmsValue* values, int count);
    
    /**
     * @brief 获取服务器状态
     * @return 运行状态
     */
    bool isRunning() const { return running_; }
    
    /**
     * @brief 获取当前连接的客户端数量
     * @return 客户端数量
     */
    int getClientCount() const { return clientCount_; }

    /**
     * @brief 获取服务器配置
     * @return 配置引用
     */
    const IEC61850ServerConfig& getConfig() const { return config_; }

    // ========== GOOSE/SV 管理接口 ==========

    /**
     * @brief 添加 GOOSE 发布器
     * @param config GOOSE 通信配置
     * @return GOOSE 发布器 ID，失败返回 -1
     */
    int addGoosePublisher(const GooseCommConfig& config);

    /**
     * @brief 移除 GOOSE 发布器
     * @param publisherId 发布器 ID
     * @return 成功返回 true
     */
    bool removeGoosePublisher(int publisherId);

    /**
     * @brief 获取 GOOSE 发布器
     * @param publisherId 发布器 ID
     * @return 发布器指针，无效 ID 返回 nullptr
     */
    class GooseAdapter* getGoosePublisher(int publisherId) const;

    /**
     * @brief 获取 GOOSE 发布器数量
     */
    size_t getGoosePublisherCount() const { return goosePublishers_.size(); }

    /**
     * @brief 添加 SV 发布器
     * @param config SV 通信配置
     * @return SV 发布器 ID，失败返回 -1
     */
    int addSVPublisher(const SVCommConfig& config);

    /**
     * @brief 移除 SV 发布器
     * @param publisherId 发布器 ID
     * @return 成功返回 true
     */
    bool removeSVPublisher(int publisherId);

    /**
     * @brief 获取 SV 发布器
     * @param publisherId 发布器 ID
     * @return 发布器指针，无效 ID 返回 nullptr
     */
    class SVAdapter* getSVPublisher(int publisherId) const;

    /**
     * @brief 获取 SV 发布器数量
     */
    size_t getSVPublisherCount() const { return svPublishers_.size(); }

private:
    /**
     * @brief MMS读取处理函数
     */
    static MmsValue* mmsReadHandler(void* parameter, const char* objectReference);
    
    /**
     * @brief MMS写入处理函数
     */
    static MmsDataAccessError mmsWriteHandler(void* parameter, MmsDomain* domain, 
                                               const char* variableId, MmsValue* value);
    
    /**
     * @brief 控制操作处理函数
     */
    static ControlHandlerResult controlHandlerForBinaryOutput(void* parameter, 
                                                                ControlAction* action,
                                                                MmsValue* ctlVal,
                                                                bool test);
    
    /**
     * @brief 后台服务器线程
     */
    void serverThread();
    
    /**
     * @brief 解析完整引用到绑定信息
     * @param fullRef 完整引用
     * @return 绑定信息指针
     */
    IEC61850DataBinding* findBinding(const std::string& fullRef);
    
    /**
     * @brief 初始化GOOSE发布器
     */
    bool initGoosePublisher();
    
    /**
     * @brief 初始化报告控制块
     */
    bool initReportControlBlocks();
    
private:
    IedServer server_;                // libiec61850服务器句柄 (指针类型)
    IedModel* model_;                       // IED模型
    std::string modelConfig_;               // 模型配置文件路径
    
    IEC61850ServerConfig config_;           // 服务器配置
    std::atomic<bool> running_;            // 运行标志
    std::atomic<bool> asyncRunning_;       // 异步运行标志
    std::atomic<int> clientCount_;         // 客户端计数
    std::thread serverThread_;             // 服务器线程
    std::mutex bindingsMutex_;             // 绑定信息锁
    
    // 数据绑定映射: 完整引用 -> 绑定信息
    std::map<std::string, IEC61850DataBinding> dataBindings_;
    
    // 回调函数
    MMSReadCallback readCallback_;
    MMSWriteCallback writeCallback_;
    ControlCallback controlCallback_;
    
    // GOOSE发布器管理
    std::map<int, class GooseAdapter*> goosePublishers_;
    std::atomic<int> nextGoosePublisherId_;

    // SV发布器管理
    std::map<int, class SVAdapter*> svPublishers_;
    std::atomic<int> nextSVPublisherId_;
};

} // namespace IPC

#endif // HAS_LIBIEC61850

#endif // IEC61850_SERVER_H
