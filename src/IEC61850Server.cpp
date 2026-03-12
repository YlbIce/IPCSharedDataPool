/**
 * @file IEC61850Server.cpp
 * @brief IEC61850 MMS服务器封装实现
 */

#ifdef HAS_LIBIEC61850

#include "IEC61850Server.h"
#include "GooseAdapter.h"
#include "SVAdapter.h"
#include "ReportAdapter.h"
#include "LogService.h"
#include <cstring>
#include <iostream>
#include <fstream>

namespace IPC {

// ========== 构造函数和析构函数 ==========

IEC61850Server::IEC61850Server()
    : server_(nullptr)
    , model_(nullptr)
    , running_(false)
    , asyncRunning_(false)
    , clientCount_(0)
    , nextGoosePublisherId_(1)
    , nextSVPublisherId_(1)
{
}

IEC61850Server::~IEC61850Server() {
    stop();
    
    // 清理 GOOSE 发布器
    for (auto& pair : goosePublishers_) {
        if (pair.second) {
            GooseAdapter::destroy(pair.second);
        }
    }
    goosePublishers_.clear();
    
    // 清理 SV 发布器
    for (auto& pair : svPublishers_) {
        if (pair.second) {
            SVAdapter::destroy(pair.second);
        }
    }
    svPublishers_.clear();
    
    // 销毁服务器
    if (server_) {
        IedServer_destroy(server_);
        server_ = nullptr;
    }
    
    // 模型由 libiec61850 管理，不需要单独释放
    model_ = nullptr;
}

// ========== 初始化 ==========

bool IEC61850Server::initialize(const IEC61850ServerConfig& config) {
    config_ = config;
    
    // 检查模型文件是否存在
    std::ifstream modelFile(config.modelName);
    if (!modelFile.good()) {
        std::cerr << "Model file not found: " << config.modelName << std::endl;
        // 尝试创建一个简单的模型
        model_ = IedModel_create(config.iedName.c_str());
        if (!model_) {
            std::cerr << "Failed to create IED model" << std::endl;
            return false;
        }
    } else {
        // 从 SCL 文件加载模型
        model_ = ConfigFileParser_createModelFromConfigFileEx(config.modelName.c_str());
        if (!model_) {
            std::cerr << "Failed to load model from: " << config.modelName << std::endl;
            return false;
        }
    }
    
    // 创建 IED 服务器
    server_ = IedServer_create(model_);
    if (!server_) {
        std::cerr << "Failed to create IED server" << std::endl;
        return false;
    }
    
    // 设置 MMS 读写处理函数
    IedServer_setReadAccessHandler(server_, mmsReadHandler, this);
    IedServer_setWriteAccessHandler(server_, mmsWriteHandler, this);
    
    // 设置控制处理函数
    IedServer_setControlHandler(server_, controlHandlerForBinaryOutput, this);
    
    // 初始化报告控制块
    if (!initReportControlBlocks()) {
        std::cerr << "Warning: Failed to initialize report control blocks" << std::endl;
    }
    
    // 初始化 GOOSE 发布器
    if (config.enableGoose) {
        if (!initGoosePublisher()) {
            std::cerr << "Warning: Failed to initialize GOOSE publisher" << std::endl;
        }
    }
    
    std::cout << "IEC61850Server initialized on port " << config.tcpPort 
              << ", model=" << config.modelName << std::endl;
    
    return true;
}

bool IEC61850Server::initGoosePublisher() {
    // GOOSE 发布器通过 addGoosePublisher 接口动态添加
    return true;
}

bool IEC61850Server::initReportControlBlocks() {
    // 报告控制块由 SCL 文件定义，这里只是启用
    return true;
}

// ========== 启动和停止 ==========

bool IEC61850Server::run() {
    if (running_) {
        std::cerr << "Server is already running" << std::endl;
        return false;
    }
    
    if (!server_) {
        std::cerr << "Server not initialized" << std::endl;
        return false;
    }
    
    // 启动服务器（阻塞模式）
    IedServer_start(server_, config_.tcpPort);
    
    if (!IedServer_isRunning(server_)) {
        std::cerr << "Failed to start IED server" << std::endl;
        return false;
    }
    
    running_ = true;
    std::cout << "IEC61850Server started on port " << config_.tcpPort << std::endl;
    
    // 阻塞运行
    while (running_ && IedServer_isRunning(server_)) {
        Thread_sleep(100);
    }
    
    return true;
}

bool IEC61850Server::startAsync() {
    if (running_ || asyncRunning_) {
        std::cerr << "Server is already running" << std::endl;
        return false;
    }
    
    if (!server_) {
        std::cerr << "Server not initialized" << std::endl;
        return false;
    }
    
    // 启动服务器
    IedServer_start(server_, config_.tcpPort);
    
    if (!IedServer_isRunning(server_)) {
        std::cerr << "Failed to start IED server" << std::endl;
        return false;
    }
    
    running_ = true;
    asyncRunning_ = true;
    
    // 启动后台线程
    serverThread_ = std::thread(&IEC61850Server::serverThread, this);
    
    std::cout << "IEC61850Server started asynchronously on port " << config_.tcpPort << std::endl;
    
    return true;
}

void IEC61850Server::stop() {
    if (!running_ && !asyncRunning_) {
        return;
    }
    
    running_ = false;
    asyncRunning_ = false;
    
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    if (server_) {
        IedServer_stop(server_);
    }
    
    std::cout << "IEC61850Server stopped" << std::endl;
}

void IEC61850Server::serverThread() {
    while (running_ && IedServer_isRunning(server_)) {
        Thread_sleep(100);
    }
}

// ========== 回调设置 ==========

void IEC61850Server::setReadCallback(MMSReadCallback callback) {
    readCallback_ = std::move(callback);
}

void IEC61850Server::setWriteCallback(MMSWriteCallback callback) {
    writeCallback_ = std::move(callback);
}

void IEC61850Server::setControlCallback(ControlCallback callback) {
    controlCallback_ = std::move(callback);
}

// ========== 数据绑定 ==========

bool IEC61850Server::addDataBinding(const IEC61850DataBinding& binding) {
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    
    std::string fullRef = binding.getFullRef();
    if (dataBindings_.find(fullRef) != dataBindings_.end()) {
        std::cerr << "Data binding already exists: " << fullRef << std::endl;
        return false;
    }
    
    dataBindings_[fullRef] = binding;
    return true;
}

int IEC61850Server::addDataBindings(const std::vector<IEC61850DataBinding>& bindings) {
    int count = 0;
    for (const auto& binding : bindings) {
        if (addDataBinding(binding)) {
            count++;
        }
    }
    return count;
}

int IEC61850Server::loadBindingsFromSCL(const std::string& sclFile) {
    // 简化实现：从 SCL 文件解析数据绑定
    // 实际实现需要完整解析 SCL XML 结构
    
    std::ifstream file(sclFile);
    if (!file.good()) {
        std::cerr << "SCL file not found: " << sclFile << std::endl;
        return 0;
    }
    
    int count = 0;
    // TODO: 使用 pugixml 解析 SCL 文件
    // 提取 DataTypeTemplates -> LNodeType -> DO -> DA 结构
    
    std::cout << "Loaded " << count << " bindings from " << sclFile << std::endl;
    return count;
}

IEC61850DataBinding* IEC61850Server::findBinding(const std::string& fullRef) {
    auto it = dataBindings_.find(fullRef);
    if (it != dataBindings_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ========== MMS 处理函数 ==========

MmsValue* IEC61850Server::mmsReadHandler(void* parameter, const char* objectReference) {
    IEC61850Server* self = static_cast<IEC61850Server*>(parameter);
    
    if (!self || !objectReference) {
        return nullptr;
    }
    
    std::string ref(objectReference);
    IEC61850DataBinding* binding = self->findBinding(ref);
    
    if (!binding) {
        // 未找到绑定，返回默认值
        return MmsValue_newBoolean(false);
    }
    
    // 调用读取回调
    if (self->readCallback_) {
        uint8_t buffer[64] = {0};
        if (self->readCallback_(*binding, buffer)) {
            // 根据 FC 类型创建 MmsValue
            return MmsValue_newBoolean(buffer[0] != 0);
        }
    }
    
    return MmsValue_newBoolean(false);
}

MmsDataAccessError IEC61850Server::mmsWriteHandler(void* parameter, MmsDomain* domain,
                                                    const char* variableId, MmsValue* value) {
    (void)domain; // 未使用
    
    IEC61850Server* self = static_cast<IEC61850Server*>(parameter);
    
    if (!self || !variableId || !value) {
        return DATA_ACCESS_ERROR_OBJECT_INVALIDATED;
    }
    
    std::string ref(variableId);
    IEC61850DataBinding* binding = self->findBinding(ref);
    
    if (!binding) {
        return DATA_ACCESS_ERROR_OBJECT_NONE_EXISTENT;
    }
    
    if (!binding->isWritable) {
        return DATA_ACCESS_ERROR_OBJECT_ACCESS_DENIED;
    }
    
    // 调用写入回调
    if (self->writeCallback_) {
        uint8_t buffer[64] = {0};
        
        // 转换 MmsValue 到缓冲区
        switch (MmsValue_getType(value)) {
            case MMS_BOOLEAN:
                buffer[0] = MmsValue_getBoolean(value) ? 1 : 0;
                break;
            case MMS_INTEGER:
                *reinterpret_cast<int32_t*>(buffer) = MmsValue_getInteger(value);
                break;
            case MMS_FLOAT:
                *reinterpret_cast<float*>(buffer) = MmsValue_getFloat(value);
                break;
            default:
                break;
        }
        
        if (self->writeCallback_(*binding, buffer)) {
            return DATA_ACCESS_ERROR_SUCCESS;
        }
    }
    
    return DATA_ACCESS_ERROR_OBJECT_ACCESS_DENIED;
}

ControlHandlerResult IEC61850Server::controlHandlerForBinaryOutput(void* parameter,
                                                                      ControlAction* action,
                                                                      MmsValue* ctlVal,
                                                                      bool test) {
    (void)action; // 未使用
    
    IEC61850Server* self = static_cast<IEC61850Server*>(parameter);
    
    if (!self || !ctlVal) {
        return CONTROL_RESULT_FAILED;
    }
    
    // 测试模式直接返回成功
    if (test) {
        return CONTROL_RESULT_OK;
    }
    
    // 获取控制值
    bool value = MmsValue_getBoolean(ctlVal);
    
    // 调用控制回调
    if (self->controlCallback_) {
        std::string ctlModel = "unknown"; // 实际应从模型中获取
        if (self->controlCallback_(ctlModel, value)) {
            return CONTROL_RESULT_OK;
        }
    }
    
    return CONTROL_RESULT_FAILED;
}

// ========== 数据变化触发 ==========

void IEC61850Server::triggerDataChange(const std::string& fullRef) {
    IEC61850DataBinding* binding = findBinding(fullRef);
    if (!binding) {
        return;
    }
    
    // 更新服务器数据模型
    // 实际实现需要根据绑定信息获取数据值并更新
    
    // 触发 GOOSE 发布
    for (auto& pair : goosePublishers_) {
        if (pair.second) {
            pair.second->publish(true);
        }
    }
}

bool IEC61850Server::publishGoose(const std::string& dataSetId, const MmsValue* values, int count) {
    (void)dataSetId;
    (void)values;
    (void)count;
    
    // 查找对应的 GOOSE 发布器
    for (auto& pair : goosePublishers_) {
        if (pair.second) {
            return pair.second->publish(true);
        }
    }
    
    return false;
}

// ========== GOOSE 发布器管理 ==========

int IEC61850Server::addGoosePublisher(const GooseCommConfig& config) {
    GooseAdapter* adapter = GooseAdapter::create(config);
    if (!adapter) {
        std::cerr << "Failed to create GOOSE publisher" << std::endl;
        return -1;
    }
    
    int id = nextGoosePublisherId_++;
    goosePublishers_[id] = adapter;
    
    std::cout << "Added GOOSE publisher id=" << id 
              << ", interface=" << config.interfaceName << std::endl;
    
    return id;
}

bool IEC61850Server::removeGoosePublisher(int publisherId) {
    auto it = goosePublishers_.find(publisherId);
    if (it == goosePublishers_.end()) {
        return false;
    }
    
    if (it->second) {
        GooseAdapter::destroy(it->second);
    }
    
    goosePublishers_.erase(it);
    std::cout << "Removed GOOSE publisher id=" << publisherId << std::endl;
    
    return true;
}

GooseAdapter* IEC61850Server::getGoosePublisher(int publisherId) const {
    auto it = goosePublishers_.find(publisherId);
    if (it != goosePublishers_.end()) {
        return it->second;
    }
    return nullptr;
}

// ========== SV 发布器管理 ==========

int IEC61850Server::addSVPublisher(const SVCommConfig& config) {
    SVAdapter* adapter = SVAdapter::create(config);
    if (!adapter) {
        std::cerr << "Failed to create SV publisher" << std::endl;
        return -1;
    }
    
    int id = nextSVPublisherId_++;
    svPublishers_[id] = adapter;
    
    std::cout << "Added SV publisher id=" << id 
              << ", interface=" << config.interfaceName << std::endl;
    
    return id;
}

bool IEC61850Server::removeSVPublisher(int publisherId) {
    auto it = svPublishers_.find(publisherId);
    if (it == svPublishers_.end()) {
        return false;
    }
    
    if (it->second) {
        SVAdapter::destroy(it->second);
    }
    
    svPublishers_.erase(it);
    std::cout << "Removed SV publisher id=" << publisherId << std::endl;
    
    return true;
}

SVAdapter* IEC61850Server::getSVPublisher(int publisherId) const {
    auto it = svPublishers_.find(publisherId);
    if (it != svPublishers_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace IPC

#endif // HAS_LIBIEC61850
