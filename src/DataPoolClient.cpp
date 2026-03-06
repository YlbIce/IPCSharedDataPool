/**
 * @file DataPoolClient.cpp
 * @brief 数据池客户端实现
 */

#include "../include/DataPoolClient.h"
#include <cstring>

namespace IPC {

DataPoolClient* DataPoolClient::init(const Config& config) {
    DataPoolClient* client = new DataPoolClient();
    client->m_processName = config.processName;
    client->m_isCreator = config.create;
    client->m_processId = INVALID_INDEX;
    client->m_dataPool = nullptr;
    client->m_eventCenter = nullptr;
    client->m_soeRecorder = nullptr;
    client->m_persistentStorage = nullptr;
    client->m_votingEngine = nullptr;
    client->m_iec61850Mapper = nullptr;
    
    // 初始化数据池
    if (config.create) {
        client->m_dataPool = SharedDataPool::create(
            config.poolName.c_str(),
            config.yxCount, config.ycCount,
            config.dzCount, config.ykCount);
    } else {
        client->m_dataPool = SharedDataPool::connect(config.poolName.c_str());
    }
    
    if (!client->m_dataPool || !client->m_dataPool->isValid()) {
        delete client;
        return nullptr;
    }
    
    // 初始化事件中心
    if (config.create) {
        client->m_eventCenter = IPCEventCenter::create(
            config.eventName.c_str(), config.eventCapacity);
    } else {
        client->m_eventCenter = IPCEventCenter::connect(config.eventName.c_str());
    }
    
    if (!client->m_eventCenter || !client->m_eventCenter->isValid()) {
        client->m_dataPool->disconnect();
        delete client->m_dataPool;
        delete client;
        return nullptr;
    }
    
    // 初始化SOE记录器
    if (config.enableSOE) {
        if (config.create) {
            client->m_soeRecorder = SOERecorder::create(
                config.soeName.c_str(), config.soeCapacity);
        } else {
            client->m_soeRecorder = SOERecorder::connect(config.soeName.c_str());
        }
        
        if (!client->m_soeRecorder || !client->m_soeRecorder->isValid()) {
            // SOE不是必须的，继续初始化
            client->m_soeRecorder = nullptr;
        }
    }
    
    // 初始化持久化存储
    if (config.enablePersistence && client->m_dataPool) {
        client->m_persistentStorage = std::make_unique<PersistentStorage>(
            client->m_dataPool, config.persistConfig);
    }
    
    // 初始化表决引擎
    if (config.enableVoting) {
        VotingEngine::ShmConfig votingConfig = config.votingConfig;
        votingConfig.create = config.create;
        client->m_votingEngine = VotingEngine::create(votingConfig);
    }
    
    // 初始化IEC61850映射器
    if (config.enableIEC61850) {
        IEC61850Mapper::Config iecConfig = config.iec61850Config;
        iecConfig.create = config.create;
        client->m_iec61850Mapper = IEC61850Mapper::create(iecConfig);
    }
    
    // 注册进程
    if (client->m_dataPool->registerProcess(config.processName.c_str(), client->m_processId) 
        != Result::OK) {
        client->m_processId = INVALID_INDEX;
    }
    
    return client;
}

void DataPoolClient::shutdown() {
    // 停止心跳和健康监控线程
    stopHeartbeat();
    stopHealthMonitor();
    
    // 先销毁持久化存储（它会停止后台线程）
    m_persistentStorage.reset();
    
    // 销毁IEC61850映射器
    // 注意：destroy() 内部会 delete this，所以不要再 delete
    if (m_iec61850Mapper) {
        if (m_isCreator) {
            m_iec61850Mapper->destroy();
        } else {
            delete m_iec61850Mapper;
        }
        m_iec61850Mapper = nullptr;
    }
    
    // 销毁表决引擎
    // 注意：destroy() 内部会 delete this，所以不要再 delete
    if (m_votingEngine) {
        if (m_isCreator) {
            m_votingEngine->destroy();
        } else {
            delete m_votingEngine;
        }
        m_votingEngine = nullptr;
    }
    
    if (m_soeRecorder) {
        if (m_isCreator) {
            m_soeRecorder->destroy();
        } else {
            m_soeRecorder->disconnect();
        }
        m_soeRecorder = nullptr;
    }
    
    if (m_processId != INVALID_INDEX && m_dataPool) {
        m_dataPool->unregisterProcess(m_processId);
    }
    
    if (m_eventCenter) {
        if (m_isCreator) {
            m_eventCenter->destroy();
        } else {
            m_eventCenter->disconnect();
        }
        delete m_eventCenter;
        m_eventCenter = nullptr;
    }
    
    if (m_dataPool) {
        if (m_isCreator) {
            m_dataPool->destroy();
        } else {
            m_dataPool->disconnect();
        }
        delete m_dataPool;
        m_dataPool = nullptr;
    }
}

// ========== 数据操作 ==========

bool DataPoolClient::setYX(uint64_t key, uint8_t value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setYX(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getYX(uint64_t key, uint8_t& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getYX(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::setYC(uint64_t key, float value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setYC(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getYC(uint64_t key, float& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getYC(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::setDZ(uint64_t key, float value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setDZ(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getDZ(uint64_t key, float& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getDZ(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::setYK(uint64_t key, uint8_t value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setYK(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getYK(uint64_t key, uint8_t& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getYK(key, value, ts, quality) == Result::OK;
}

bool DataPoolClient::setYXByIndex(uint32_t index, uint8_t value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setYXByIndex(index, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getYXByIndex(uint32_t index, uint8_t& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getYXByIndex(index, value, ts, quality) == Result::OK;
}

bool DataPoolClient::setYCByIndex(uint32_t index, float value, uint8_t quality) {
    if (!m_dataPool) return false;
    uint64_t ts = getCurrentTimestamp();
    return m_dataPool->setYCByIndex(index, value, ts, quality) == Result::OK;
}

bool DataPoolClient::getYCByIndex(uint32_t index, float& value, uint8_t& quality) {
    if (!m_dataPool) return false;
    uint64_t ts;
    return m_dataPool->getYCByIndex(index, value, ts, quality) == Result::OK;
}

// ========== 点位注册 ==========

bool DataPoolClient::registerPoint(uint64_t key, PointType type, uint32_t& index) {
    if (!m_dataPool) return false;
    return m_dataPool->registerKey(key, type, index) == Result::OK;
}

bool DataPoolClient::findPoint(uint64_t key, PointType& type, uint32_t& index) {
    if (!m_dataPool) return false;
    return m_dataPool->findKey(key, type, index) == Result::OK;
}

// ========== 事件操作 ==========

bool DataPoolClient::publishEvent(uint64_t key, PointType type, uint32_t oldValue, uint32_t newValue) {
    if (!m_eventCenter) return false;
    return m_eventCenter->publishDataChange(key, type, oldValue, newValue, m_processName.c_str()) 
           == Result::OK;
}

bool DataPoolClient::publishEvent(uint64_t key, PointType type, float oldValue, float newValue) {
    if (!m_eventCenter) return false;
    return m_eventCenter->publishDataChange(key, type, oldValue, newValue, m_processName.c_str()) 
           == Result::OK;
}

uint32_t DataPoolClient::subscribe(std::function<void(const Event&)> callback) {
    if (!m_eventCenter) return INVALID_INDEX;
    uint32_t subId;
    if (m_eventCenter->subscribe(callback, subId) == Result::OK) {
        return subId;
    }
    return INVALID_INDEX;
}

bool DataPoolClient::unsubscribe(uint32_t subscriberId) {
    if (!m_eventCenter) return false;
    return m_eventCenter->unsubscribe(subscriberId) == Result::OK;
}

uint32_t DataPoolClient::processEvents(uint32_t subscriberId, uint32_t maxEvents) {
    if (!m_eventCenter) return 0;
    return m_eventCenter->process(subscriberId, maxEvents);
}

bool DataPoolClient::pollEvent(uint32_t subscriberId, Event& event) {
    if (!m_eventCenter) return false;
    return m_eventCenter->poll(subscriberId, event) == Result::OK;
}

// ========== 便捷方法 ==========

bool DataPoolClient::setYXWithEvent(uint64_t key, uint8_t value, uint8_t quality) {
    uint8_t oldValue = 0, oldQuality = 0;
    getYX(key, oldValue, oldQuality);
    
    if (!setYX(key, value, quality)) {
        return false;
    }
    
    publishEvent(key, PointType::YX, uint32_t(oldValue), uint32_t(value));
    return true;
}

bool DataPoolClient::setYCWithEvent(uint64_t key, float value, uint8_t quality) {
    float oldValue = 0;
    uint8_t oldQuality = 0;
    getYC(key, oldValue, oldQuality);
    
    if (!setYC(key, value, quality)) {
        return false;
    }
    
    publishEvent(key, PointType::YC, oldValue, value);
    return true;
}

// ========== 状态查询 ==========

bool DataPoolClient::isValid() const {
    return m_dataPool && m_dataPool->isValid() && 
           m_eventCenter && m_eventCenter->isValid();
}

// ========== 进程管理 ==========

void DataPoolClient::updateHeartbeat() {
    if (m_dataPool && m_processId != INVALID_INDEX) {
        m_dataPool->updateHeartbeat(m_processId);
    }
}

ProcessHealth DataPoolClient::checkProcessHealth(uint32_t processId) {
    if (!m_dataPool) return ProcessHealth::UNKNOWN;
    return m_dataPool->checkProcessHealth(processId);
}

uint32_t DataPoolClient::getActiveProcessList(uint32_t* processIds, uint32_t maxCount) {
    if (!m_dataPool) return 0;
    return m_dataPool->getActiveProcessList(processIds, maxCount);
}

uint32_t DataPoolClient::cleanupDeadProcesses() {
    if (!m_dataPool) return 0;
    return m_dataPool->cleanupDeadProcesses();
}

// ========== 统计信息 ==========

DataPoolStats DataPoolClient::getStats() const {
    if (!m_dataPool) return DataPoolStats();
    return m_dataPool->getStats();
}

void DataPoolClient::resetStats() {
    if (m_dataPool) {
        m_dataPool->resetStats();
    }
}

// ========== 快照持久化 ==========

bool DataPoolClient::saveSnapshot(const char* filename) {
    if (!m_dataPool || !filename) return false;
    return m_dataPool->saveSnapshot(filename) == Result::OK;
}

bool DataPoolClient::loadSnapshot(const char* filename) {
    if (!m_dataPool || !filename) return false;
    return m_dataPool->loadSnapshot(filename) == Result::OK;
}

bool DataPoolClient::validateSnapshot(const char* filename) {
    if (!m_dataPool || !filename) return false;
    return m_dataPool->validateSnapshot(filename);
}

// ========== SOE 事件记录 ==========

bool DataPoolClient::recordSOE(const SOERecord& record) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->record(record) == Result::OK;
}

bool DataPoolClient::recordSOEYXChange(uint32_t pointKey, uint8_t oldValue, 
                                        uint8_t newValue, uint8_t priority) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->recordYXChange(pointKey, oldValue, newValue, priority) == Result::OK;
}

bool DataPoolClient::recordSOEYKExecute(uint32_t pointKey, uint8_t command, 
                                         uint8_t priority) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->recordYKExecute(pointKey, command, priority) == Result::OK;
}

bool DataPoolClient::recordSOEProtectionAct(uint32_t pointKey, uint8_t action,
                                             uint8_t priority) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->recordProtectionAct(pointKey, action, priority) == Result::OK;
}

bool DataPoolClient::querySOE(const SOEQueryCondition& condition,
                               SOERecord* records, uint32_t& count, uint32_t maxCount) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->query(condition, records, count, maxCount) == Result::OK;
}

bool DataPoolClient::getLatestSOE(uint32_t count, SOERecord* records, uint32_t& actualCount) {
    if (!m_soeRecorder) return false;
    return m_soeRecorder->getLatest(count, records, actualCount) == Result::OK;
}

SOEStats DataPoolClient::getSOEStats() const {
    if (!m_soeRecorder) return SOEStats();
    return m_soeRecorder->getStats();
}

bool DataPoolClient::exportSOEToCSV(const char* filename, 
                                     const SOEQueryCondition* condition) {
    if (!m_soeRecorder || !filename) return false;
    return m_soeRecorder->exportToCSV(filename, condition) == Result::OK;
}

// ========== 持久化管理 ==========

bool DataPoolClient::initializeData() {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->initialize() == Result::OK;
}

bool DataPoolClient::restoreFromSnapshot(const char* filename) {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->restore(filename) == Result::OK;
}

void DataPoolClient::enableAutoSnapshot(bool enable) {
    if (m_persistentStorage) {
        m_persistentStorage->enableAutoSnapshot(enable);
    }
}

void DataPoolClient::setSnapshotInterval(uint32_t intervalMs) {
    if (m_persistentStorage) {
        m_persistentStorage->setSnapshotInterval(intervalMs);
    }
}

bool DataPoolClient::triggerSnapshot() {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->triggerSnapshot() == Result::OK;
}

bool DataPoolClient::createBackup() {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->createBackup() == Result::OK;
}

bool DataPoolClient::restoreFromBackup(uint32_t backupIndex) {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->restoreFromBackup(backupIndex) == Result::OK;
}

std::vector<std::string> DataPoolClient::getBackupList() const {
    if (!m_persistentStorage) return std::vector<std::string>();
    return m_persistentStorage->getBackupList();
}

bool DataPoolClient::hasValidSnapshot() const {
    if (!m_persistentStorage) return false;
    return m_persistentStorage->hasValidSnapshot();
}

// ========== 三取二表决 ==========

uint32_t DataPoolClient::addVotingGroup(const VotingConfig& config) {
    if (!m_votingEngine) return INVALID_INDEX;
    return m_votingEngine->addVotingGroup(config);
}

bool DataPoolClient::performVotingYX(uint32_t groupId, VotingOutput& output) {
    if (!m_votingEngine || !m_dataPool) return false;
    
    // 获取表决组配置
    VotingConfig config;
    if (!m_votingEngine->getVotingGroupConfig(groupId, config)) {
        return false;
    }
    
    // 从数据池读取三个源的值
    SourceData sources[3] = {};
    uint64_t sourceKeys[3] = {config.sourceKeyA, config.sourceKeyB, config.sourceKeyC};
    
    for (int i = 0; i < 3; i++) {
        uint8_t value = 0, quality = 0;
        if (m_dataPool->getYX(sourceKeys[i], value, sources[i].timestamp, quality) == Result::OK) {
            sources[i].yxValue = value;
            sources[i].quality = quality;
            sources[i].status = (quality == 0) ? static_cast<uint8_t>(SourceStatus::VALID) 
                                                : static_cast<uint8_t>(SourceStatus::INVALID);
        } else {
            sources[i].status = static_cast<uint8_t>(SourceStatus::DISCONNECTED);
        }
    }
    
    return m_votingEngine->voteYX(groupId, sources, output);
}

bool DataPoolClient::performVotingYC(uint32_t groupId, VotingOutput& output) {
    if (!m_votingEngine || !m_dataPool) return false;
    
    // 获取表决组配置
    VotingConfig config;
    if (!m_votingEngine->getVotingGroupConfig(groupId, config)) {
        return false;
    }
    
    // 从数据池读取三个源的值
    SourceData sources[3] = {};
    uint64_t sourceKeys[3] = {config.sourceKeyA, config.sourceKeyB, config.sourceKeyC};
    
    for (int i = 0; i < 3; i++) {
        float value = 0;
        uint8_t quality = 0;
        if (m_dataPool->getYC(sourceKeys[i], value, sources[i].timestamp, quality) == Result::OK) {
            sources[i].ycValue = value;
            sources[i].quality = quality;
            sources[i].status = (quality == 0) ? static_cast<uint8_t>(SourceStatus::VALID) 
                                                : static_cast<uint8_t>(SourceStatus::INVALID);
        } else {
            sources[i].status = static_cast<uint8_t>(SourceStatus::DISCONNECTED);
        }
    }
    
    return m_votingEngine->voteYC(groupId, sources, output);
}

void DataPoolClient::setVotingAlarmCallback(VotingEngine::AlarmCallback callback) {
    if (m_votingEngine) {
        m_votingEngine->setAlarmCallback(callback);
    }
}

// ========== IEC 61850 映射 ==========

uint32_t DataPoolClient::addIEC61850Mapping(const DAMapping& mapping) {
    if (!m_iec61850Mapper) return INVALID_INDEX;
    return m_iec61850Mapper->addDAMapping(mapping);
}

bool DataPoolClient::loadSCLConfig(const char* sclFile) {
    if (!m_iec61850Mapper || !sclFile) return false;
    return m_iec61850Mapper->importFromSCL(sclFile);
}

bool DataPoolClient::exportSCLConfig(const char* sclFile) {
    if (!m_iec61850Mapper || !sclFile) return false;
    return m_iec61850Mapper->exportToSCL(sclFile);
}

void DataPoolClient::syncToIEC61850() {
    if (!m_iec61850Mapper || !m_dataPool) return;
    
    uint32_t count = m_iec61850Mapper->getMappingCount();
    for (uint32_t i = 0; i < count; i++) {
        DAMapping mapping;
        // 简化：假设可以遍历映射
        // 实际需要更完整的接口支持
    }
}

void DataPoolClient::syncFromIEC61850() {
    if (!m_iec61850Mapper || !m_dataPool) return;
    
    // 从IEC61850映射写入数据池
    // 需要更完整的接口支持
}

// ========== 心跳与健康监控 ==========

void DataPoolClient::startHeartbeat(uint32_t intervalMs) {
    if (m_heartbeatRunning.load()) {
        return;  // 已经在运行
    }
    
    m_heartbeatIntervalMs = intervalMs;
    m_heartbeatRunning.store(true);
    m_heartbeatThread = std::thread(&DataPoolClient::heartbeatThreadFunc, this);
}

void DataPoolClient::stopHeartbeat() {
    if (!m_heartbeatRunning.load()) {
        return;
    }
    
    m_heartbeatRunning.store(false);
    if (m_heartbeatThread.joinable()) {
        m_heartbeatThread.join();
    }
}

void DataPoolClient::heartbeatThreadFunc() {
    while (m_heartbeatRunning.load()) {
        updateHeartbeat();
        
        // 分段睡眠，以便能够快速响应停止请求
        for (uint32_t i = 0; i < m_heartbeatIntervalMs && m_heartbeatRunning.load(); i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void DataPoolClient::startHealthMonitor(uint32_t checkIntervalMs,
                                         std::function<void(uint32_t, ProcessHealth, ProcessHealth)> callback) {
    if (m_healthMonitorRunning.load()) {
        return;  // 已经在运行
    }
    
    m_healthCheckIntervalMs = checkIntervalMs;
    m_healthChangeCallback = callback;
    
    // 初始化健康状态缓存
    for (size_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        m_processHealthCache[i] = ProcessHealth::UNKNOWN;
    }
    
    m_healthMonitorRunning.store(true);
    m_healthMonitorThread = std::thread(&DataPoolClient::healthMonitorThreadFunc, this);
}

void DataPoolClient::stopHealthMonitor() {
    if (!m_healthMonitorRunning.load()) {
        return;
    }
    
    m_healthMonitorRunning.store(false);
    if (m_healthMonitorThread.joinable()) {
        m_healthMonitorThread.join();
    }
}

void DataPoolClient::healthMonitorThreadFunc() {
    while (m_healthMonitorRunning.load()) {
        if (m_dataPool) {
            // 获取活跃进程列表
            uint32_t processIds[MAX_PROCESS_COUNT];
            uint32_t count = m_dataPool->getActiveProcessList(processIds, MAX_PROCESS_COUNT);
            
            // 检查每个进程的健康状态
            for (uint32_t i = 0; i < count; i++) {
                uint32_t pid = processIds[i];
                
                // 跳过自己
                if (pid == m_processId) {
                    continue;
                }
                
                ProcessHealth oldHealth = m_processHealthCache[pid];
                ProcessHealth newHealth = m_dataPool->checkProcessHealth(pid);
                
                // 检测状态变化
                if (newHealth != oldHealth) {
                    m_processHealthCache[pid] = newHealth;
                    
                    // 触发回调
                    if (m_healthChangeCallback) {
                        m_healthChangeCallback(pid, oldHealth, newHealth);
                    }
                    
                    // 进程死亡，触发死亡回调
                    if (newHealth == ProcessHealth::DEAD && m_processDeathCallback) {
                        ProcessInfo info;
                        if (m_dataPool->getProcessInfo(pid, info) == Result::OK) {
                            m_processDeathCallback(pid, info.pid, info.name);
                        }
                    }
                }
            }
            
            // 定期清理死亡进程
            m_dataPool->cleanupDeadProcesses();
        }
        
        // 分段睡眠，以便能够快速响应停止请求
        for (uint32_t i = 0; i < m_healthCheckIntervalMs && m_healthMonitorRunning.load(); i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void DataPoolClient::setProcessDeathCallback(std::function<void(uint32_t, pid_t, const char*)> callback) {
    m_processDeathCallback = callback;
}

bool DataPoolClient::isHeartbeatRunning() const {
    return m_heartbeatRunning.load();
}

bool DataPoolClient::isHealthMonitorRunning() const {
    return m_healthMonitorRunning.load();
}

} // namespace IPC
