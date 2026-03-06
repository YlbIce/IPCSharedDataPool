#include "VotingEngine.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace IPC {

// 魔数和版本
constexpr uint32_t VOTING_MAGIC = 0x564F5420;  // "VOT "
constexpr uint32_t VOTING_VERSION = 1;

// 计算共享内存大小
static size_t calculateShmSize(uint32_t maxGroups) {
    size_t headerSize = sizeof(VotingEngine::ShmHeader);
    size_t configSize = maxGroups * sizeof(VotingConfig);
    size_t outputSize = maxGroups * sizeof(VotingOutput);
    size_t statsSize = maxGroups * sizeof(VotingStats);
    size_t healthSize = maxGroups * sizeof(uint8_t);
    size_t alarmSize = maxGroups * sizeof(uint16_t);
    
    // 添加告警队列区域
    size_t alarmQueueSize = maxGroups * sizeof(uint32_t) * 2; // groupId + level
    
    return headerSize + configSize + outputSize + statsSize + 
           healthSize + alarmSize + alarmQueueSize;
}

VotingEngine* VotingEngine::create(const ShmConfig& config) {
    // 计算共享内存大小
    size_t shmSize = calculateShmSize(config.maxGroups);
    
    int fd = -1;
    void* shm = nullptr;
    bool isCreator = config.create;
    
    if (config.create) {
        // 创建新的共享内存
        fd = shm_open(config.shmName.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            return nullptr;
        }
        
        if (ftruncate(fd, shmSize) < 0) {
            ::close(fd);
            shm_unlink(config.shmName.c_str());
            return nullptr;
        }
    } else {
        // 连接现有共享内存
        fd = shm_open(config.shmName.c_str(), O_RDWR, 0666);
        if (fd < 0) {
            return nullptr;
        }
    }
    
    // 映射共享内存
    shm = mmap(nullptr, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    
    if (shm == MAP_FAILED) {
        if (config.create) {
            shm_unlink(config.shmName.c_str());
        }
        return nullptr;
    }
    
    // 创建VotingEngine对象
    VotingEngine* engine = new VotingEngine();
    engine->m_shm = shm;
    engine->m_shmSize = shmSize;
    engine->m_isCreator = isCreator;
    
    // 初始化头部和数据区指针
    ShmHeader* header = static_cast<ShmHeader*>(shm);
    uint8_t* dataPtr = static_cast<uint8_t*>(shm) + sizeof(ShmHeader);
    
    if (config.create) {
        // 初始化头部
        std::memset(shm, 0, shmSize);
        header->magic = VOTING_MAGIC;
        header->version = VOTING_VERSION;
        header->maxGroups = config.maxGroups;
        header->groupCount = 0;
        header->activeAlarmCount.store(0);
        header->createTime = getCurrentTimestamp();
    } else {
        // 验证现有共享内存
        if (header->magic != VOTING_MAGIC || header->version != VOTING_VERSION) {
            munmap(shm, shmSize);
            delete engine;
            return nullptr;
        }
    }
    
    // 设置数据区指针
    engine->m_configs = reinterpret_cast<VotingConfig*>(dataPtr);
    dataPtr += config.maxGroups * sizeof(VotingConfig);
    
    engine->m_outputs = reinterpret_cast<VotingOutput*>(dataPtr);
    dataPtr += config.maxGroups * sizeof(VotingOutput);
    
    engine->m_stats = reinterpret_cast<VotingStats*>(dataPtr);
    dataPtr += config.maxGroups * sizeof(VotingStats);
    
    engine->m_healthFlags = dataPtr;
    dataPtr += config.maxGroups * sizeof(uint8_t);
    
    engine->m_alarmFlags = reinterpret_cast<uint16_t*>(dataPtr);
    
    return engine;
}

void VotingEngine::destroy() {
    if (m_shm) {
        ShmHeader* header = static_cast<ShmHeader*>(m_shm);
        
        // 取消映射
        munmap(m_shm, m_shmSize);
        
        // 如果是创建者，删除共享内存
        if (m_isCreator) {
            // 注意：这里假设使用默认名称，实际应存储名称
        }
        
        m_shm = nullptr;
    }
    delete this;
}

uint32_t VotingEngine::addVotingGroup(const VotingConfig& config) {
    if (!m_shm) return INVALID_INDEX;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (header->groupCount >= header->maxGroups) {
        return INVALID_INDEX;
    }
    
    // 检查是否已存在
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == config.groupId) {
            return INVALID_INDEX; // 已存在
        }
    }
    
    uint32_t index = header->groupCount++;
    
    // 复制配置
    m_configs[index] = config;
    
    // 初始化输出
    std::memset(&m_outputs[index], 0, sizeof(VotingOutput));
    m_outputs[index].groupId = config.groupId;
    
    // 初始化统计
    std::memset(&m_stats[index], 0, sizeof(VotingStats));
    
    // 初始化健康标志
    m_healthFlags[index] = 0;
    m_alarmFlags[index] = 0;
    
    return index;
}

bool VotingEngine::removeVotingGroup(uint32_t groupId) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            // 移动最后一个到当前位置
            if (i < header->groupCount - 1) {
                m_configs[i] = m_configs[header->groupCount - 1];
                m_outputs[i] = m_outputs[header->groupCount - 1];
                m_stats[i] = m_stats[header->groupCount - 1];
                m_healthFlags[i] = m_healthFlags[header->groupCount - 1];
                m_alarmFlags[i] = m_alarmFlags[header->groupCount - 1];
            }
            header->groupCount--;
            return true;
        }
    }
    
    return false;
}

bool VotingEngine::updateVotingGroup(uint32_t groupId, const VotingConfig& config) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            m_configs[i] = config;
            return true;
        }
    }
    
    return false;
}

bool VotingEngine::getVotingGroupConfig(uint32_t groupId, VotingConfig& config) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            config = m_configs[i];
            return true;
        }
    }
    
    return false;
}

uint32_t VotingEngine::getVotingGroupCount() {
    if (!m_shm) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    return header->groupCount;
}

VotingResult VotingEngine::performVotingYX(const SourceData sources[3], 
                                            const VotingConfig& config, 
                                            VotingOutput& output) {
    // 统计有效源
    uint8_t validCount = 0;
    uint8_t validValues[3];
    uint8_t validIndices[3];
    
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID) &&
            sources[i].quality == 0) {
            validValues[validCount] = sources[i].yxValue;
            validIndices[validCount] = i;
            validCount++;
        }
    }
    
    output.validSourceCount = validCount;
    
    // 检查有效源数量
    if (validCount < 2) {
        output.result = static_cast<uint8_t>(VotingResult::INSUFFICIENT);
        output.quality = 0xFF; // 无效质量码
        return VotingResult::INSUFFICIENT;
    }
    
    // 根据策略进行表决
    switch (config.votingStrategy) {
        case 0: // 严格三取二
            if (validCount == 3) {
                if (validValues[0] == validValues[1] && validValues[1] == validValues[2]) {
                    output.result = static_cast<uint8_t>(VotingResult::UNANIMOUS);
                    output.yxValue = validValues[0];
                    output.quality = 0;
                    return VotingResult::UNANIMOUS;
                } else if (validValues[0] == validValues[1] ||
                           validValues[1] == validValues[2] ||
                           validValues[0] == validValues[2]) {
                    output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
                    // 找出多数值
                    if (validValues[0] == validValues[1]) {
                        output.yxValue = validValues[0];
                    } else if (validValues[1] == validValues[2]) {
                        output.yxValue = validValues[1];
                    } else {
                        output.yxValue = validValues[0];
                    }
                    output.quality = 0;
                    return VotingResult::MAJORITY;
                } else {
                    output.result = static_cast<uint8_t>(VotingResult::DISAGREE);
                    output.quality = 0x02; // QUESTIONABLE
                    return VotingResult::DISAGREE;
                }
            } else { // validCount == 2
                if (validValues[0] == validValues[1]) {
                    output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
                    output.yxValue = validValues[0];
                    output.quality = 0x04; // OLD_DATA (表示缺一个源)
                    return VotingResult::MAJORITY;
                } else {
                    output.result = static_cast<uint8_t>(VotingResult::DISAGREE);
                    output.quality = 0x02;
                    return VotingResult::DISAGREE;
                }
            }
            break;
            
        case 1: // 宽松表决
            if (validCount >= 2) {
                // 找出一致的源
                for (int i = 0; i < validCount - 1; i++) {
                    for (int j = i + 1; j < validCount; j++) {
                        if (validValues[i] == validValues[j]) {
                            if (validCount == 3 && 
                                validValues[0] == validValues[1] && 
                                validValues[1] == validValues[2]) {
                                output.result = static_cast<uint8_t>(VotingResult::UNANIMOUS);
                            } else {
                                output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
                            }
                            output.yxValue = validValues[i];
                            output.quality = 0;
                            return static_cast<VotingResult>(output.result);
                        }
                    }
                }
                output.result = static_cast<uint8_t>(VotingResult::DISAGREE);
                output.quality = 0x02;
                return VotingResult::DISAGREE;
            }
            break;
            
        case 2: // 优先级表决
            {
                uint8_t priorityIdx = config.prioritySource;
                // 检查优先源是否有效
                if (sources[priorityIdx].status == static_cast<uint8_t>(SourceStatus::VALID) &&
                    sources[priorityIdx].quality == 0) {
                    output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
                    output.yxValue = sources[priorityIdx].yxValue;
                    output.quality = 0;
                    return VotingResult::MAJORITY;
                }
                // 优先源无效，使用其他两个
                for (int i = 0; i < 3; i++) {
                    if (i != priorityIdx && 
                        sources[i].status == static_cast<uint8_t>(SourceStatus::VALID)) {
                        output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
                        output.yxValue = sources[i].yxValue;
                        output.quality = 0x04;
                        return VotingResult::MAJORITY;
                    }
                }
            }
            break;
    }
    
    output.result = static_cast<uint8_t>(VotingResult::FAILED);
    output.quality = 0xFF;
    return VotingResult::FAILED;
}

VotingResult VotingEngine::performVotingYC(const SourceData sources[3], 
                                            const VotingConfig& config, 
                                            VotingOutput& output) {
    // 统计有效源
    uint8_t validCount = 0;
    float validValues[3];
    
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID) &&
            sources[i].quality == 0) {
            validValues[validCount++] = sources[i].ycValue;
        }
    }
    
    output.validSourceCount = validCount;
    
    if (validCount < 2) {
        output.result = static_cast<uint8_t>(VotingResult::INSUFFICIENT);
        output.quality = 0xFF;
        return VotingResult::INSUFFICIENT;
    }
    
    // 计算平均值
    float sum = 0;
    for (int i = 0; i < validCount; i++) {
        sum += validValues[i];
    }
    float avg = sum / validCount;
    
    // 检查偏差
    float maxDeviation = 0;
    for (int i = 0; i < validCount; i++) {
        float dev = std::abs(validValues[i] - avg);
        if (dev > maxDeviation) {
            maxDeviation = dev;
        }
    }
    
    // 判断结果
    if (validCount == 3) {
        if (maxDeviation <= config.deviationLimit) {
            output.result = static_cast<uint8_t>(VotingResult::UNANIMOUS);
        } else {
            // 剔除偏差最大的源
            int maxIdx = 0;
            maxDeviation = 0;
            for (int i = 0; i < 3; i++) {
                float dev = std::abs(validValues[i] - avg);
                if (dev > maxDeviation) {
                    maxDeviation = dev;
                    maxIdx = i;
                }
            }
            // 重新计算平均值（排除最大偏差源）
            sum = 0;
            for (int i = 0; i < 3; i++) {
                if (i != maxIdx) {
                    sum += validValues[i];
                }
            }
            avg = sum / 2;
            output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
        }
    } else {
        output.result = static_cast<uint8_t>(VotingResult::MAJORITY);
    }
    
    output.ycValue = avg;
    output.quality = (validCount < 3) ? 0x04 : 0;
    
    return static_cast<VotingResult>(output.result);
}

void VotingEngine::checkDeviationYX(const SourceData sources[3], 
                                     const VotingConfig& config,
                                     VotingOutput& output) {
    if (!config.enableDeviation) return;
    
    uint8_t validCount = output.validSourceCount;
    if (validCount < 2) return;
    
    // 统计不同值的数量
    uint8_t count0 = 0, count1 = 0;
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID)) {
            if (sources[i].yxValue == 0) count0++;
            else count1++;
        }
    }
    
    // YX偏差检测：只要有源不一致就报警
    if (count0 > 0 && count1 > 0) {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::SEVERE);
        output.deviationCount++;
        output.alarmFlags |= 0x01;
        
        if (output.deviationCount >= config.deviationCountLimit) {
            triggerAlarm(output.groupId, DeviationLevel::SEVERE, "YX deviation detected");
        }
    } else {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::NONE);
        output.deviationCount = 0;
    }
}

void VotingEngine::checkDeviationYC(const SourceData sources[3], 
                                     const VotingConfig& config,
                                     VotingOutput& output) {
    if (!config.enableDeviation) return;
    
    uint8_t validCount = output.validSourceCount;
    if (validCount < 2) return;
    
    // 计算最大偏差
    float values[3];
    int idx = 0;
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID)) {
            values[idx++] = sources[i].ycValue;
        }
    }
    
    float maxDev = 0;
    for (int i = 0; i < validCount - 1; i++) {
        for (int j = i + 1; j < validCount; j++) {
            float dev = std::abs(values[i] - values[j]);
            if (dev > maxDev) {
                maxDev = dev;
            }
        }
    }
    
    // 判断偏差级别
    float limit = config.deviationLimit;
    if (maxDev > limit * 2) {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::SEVERE);
        output.deviationCount++;
        output.alarmFlags |= 0x04;
        
        if (output.deviationCount >= config.deviationCountLimit) {
            triggerAlarm(output.groupId, DeviationLevel::SEVERE, "YC severe deviation");
        }
    } else if (maxDev > limit) {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::MODERATE);
        output.alarmFlags |= 0x02;
    } else if (maxDev > limit * 0.5) {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::MINOR);
    } else {
        output.deviationLevel = static_cast<uint8_t>(DeviationLevel::NONE);
        output.deviationCount = 0;
    }
}

void VotingEngine::triggerAlarm(uint32_t groupId, DeviationLevel level, const char* message) {
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    header->activeAlarmCount.fetch_add(1);
    
    if (m_alarmCallback) {
        m_alarmCallback(groupId, level, message);
    }
}

bool VotingEngine::voteYX(uint32_t groupId, const SourceData sources[3], VotingOutput& output) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    // 查找配置
    int configIdx = -1;
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            configIdx = i;
            break;
        }
    }
    
    if (configIdx < 0) return false;
    
    // 保存之前的偏差计数
    uint8_t prevDeviationCount = m_outputs[configIdx].deviationCount;
    
    // 初始化输出
    std::memset(&output, 0, sizeof(VotingOutput));
    output.groupId = groupId;
    output.timestamp = getCurrentTimestamp();
    for (int i = 0; i < 3; i++) {
        output.sources[i] = sources[i];
    }
    
    // 执行表决
    VotingResult result = performVotingYX(sources, m_configs[configIdx], output);
    
    // 偏差检测（传入之前的计数）
    output.deviationCount = prevDeviationCount;
    checkDeviationYX(sources, m_configs[configIdx], output);
    
    // 更新统计
    m_stats[configIdx].totalVotes++;
    m_stats[configIdx].lastVotingTime = output.timestamp;
    
    switch (result) {
        case VotingResult::UNANIMOUS:
            m_stats[configIdx].unanimousCount++;
            break;
        case VotingResult::MAJORITY:
            m_stats[configIdx].majorityCount++;
            break;
        case VotingResult::DISAGREE:
            m_stats[configIdx].disagreeCount++;
            break;
        case VotingResult::INSUFFICIENT:
            m_stats[configIdx].insufficientCount++;
            break;
        default:
            break;
    }
    
    if (output.deviationLevel > static_cast<uint8_t>(DeviationLevel::NONE)) {
        m_stats[configIdx].deviationAlarms++;
    }
    
    // 保存输出
    m_outputs[configIdx] = output;
    
    return true;
}

bool VotingEngine::voteYC(uint32_t groupId, const SourceData sources[3], VotingOutput& output) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    // 查找配置
    int configIdx = -1;
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            configIdx = i;
            break;
        }
    }
    
    if (configIdx < 0) return false;
    
    // 保存之前的偏差计数
    uint8_t prevDeviationCount = m_outputs[configIdx].deviationCount;
    
    // 初始化输出
    std::memset(&output, 0, sizeof(VotingOutput));
    output.groupId = groupId;
    output.timestamp = getCurrentTimestamp();
    for (int i = 0; i < 3; i++) {
        output.sources[i] = sources[i];
    }
    
    // 执行表决
    VotingResult result = performVotingYC(sources, m_configs[configIdx], output);
    
    // 偏差检测（传入之前的计数）
    output.deviationCount = prevDeviationCount;
    checkDeviationYC(sources, m_configs[configIdx], output);
    
    // 更新统计
    m_stats[configIdx].totalVotes++;
    m_stats[configIdx].lastVotingTime = output.timestamp;
    
    switch (result) {
        case VotingResult::UNANIMOUS:
            m_stats[configIdx].unanimousCount++;
            break;
        case VotingResult::MAJORITY:
            m_stats[configIdx].majorityCount++;
            break;
        case VotingResult::DISAGREE:
            m_stats[configIdx].disagreeCount++;
            break;
        case VotingResult::INSUFFICIENT:
            m_stats[configIdx].insufficientCount++;
            break;
        default:
            break;
    }
    
    if (output.deviationLevel > static_cast<uint8_t>(DeviationLevel::NONE)) {
        m_stats[configIdx].deviationAlarms++;
    }
    
    // 保存输出
    m_outputs[configIdx] = output;
    
    return true;
}

VotingResult VotingEngine::quickVoteYX(const SourceData sources[3], uint8_t& resultValue,
                                        uint8_t strategy) {
    // 统计有效源
    uint8_t validCount = 0;
    uint8_t validValues[3];
    
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID) &&
            sources[i].quality == 0) {
            validValues[validCount++] = sources[i].yxValue;
        }
    }
    
    if (validCount < 2) {
        return VotingResult::INSUFFICIENT;
    }
    
    // 策略0：严格三取二
    if (validCount == 3) {
        if (validValues[0] == validValues[1] && validValues[1] == validValues[2]) {
            resultValue = validValues[0];
            return VotingResult::UNANIMOUS;
        }
    }
    
    // 找多数
    if (validValues[0] == validValues[1]) {
        resultValue = validValues[0];
        return VotingResult::MAJORITY;
    }
    if (validCount == 3 && validValues[1] == validValues[2]) {
        resultValue = validValues[1];
        return VotingResult::MAJORITY;
    }
    if (validCount == 3 && validValues[0] == validValues[2]) {
        resultValue = validValues[0];
        return VotingResult::MAJORITY;
    }
    
    return VotingResult::DISAGREE;
}

VotingResult VotingEngine::quickVoteYC(const SourceData sources[3], float& resultValue,
                                        float deviationLimit, uint8_t strategy) {
    // 统计有效源
    uint8_t validCount = 0;
    float validValues[3];
    
    for (int i = 0; i < 3; i++) {
        if (sources[i].status == static_cast<uint8_t>(SourceStatus::VALID) &&
            sources[i].quality == 0) {
            validValues[validCount++] = sources[i].ycValue;
        }
    }
    
    if (validCount < 2) {
        return VotingResult::INSUFFICIENT;
    }
    
    // 计算平均值
    float sum = 0;
    for (int i = 0; i < validCount; i++) {
        sum += validValues[i];
    }
    float avg = sum / validCount;
    
    // 检查偏差
    float maxDev = 0;
    for (int i = 0; i < validCount; i++) {
        float dev = std::abs(validValues[i] - avg);
        if (dev > maxDev) {
            maxDev = dev;
        }
    }
    
    if (validCount == 3 && maxDev <= deviationLimit) {
        resultValue = avg;
        return VotingResult::UNANIMOUS;
    }
    
    // 如果偏差大，剔除最大偏差源
    if (validCount == 3 && maxDev > deviationLimit) {
        int maxIdx = 0;
        maxDev = 0;
        for (int i = 0; i < 3; i++) {
            float dev = std::abs(validValues[i] - avg);
            if (dev > maxDev) {
                maxDev = dev;
                maxIdx = i;
            }
        }
        sum = 0;
        for (int i = 0; i < 3; i++) {
            if (i != maxIdx) {
                sum += validValues[i];
            }
        }
        resultValue = sum / 2;
        return VotingResult::MAJORITY;
    }
    
    resultValue = avg;
    return VotingResult::MAJORITY;
}

bool VotingEngine::getVotingOutput(uint32_t groupId, VotingOutput& output) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            output = m_outputs[i];
            return true;
        }
    }
    
    return false;
}

bool VotingEngine::getVotingStats(uint32_t groupId, VotingStats& stats) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            stats = m_stats[i];
            return true;
        }
    }
    
    return false;
}

bool VotingEngine::resetVotingStats(uint32_t groupId) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            std::memset(&m_stats[i], 0, sizeof(VotingStats));
            return true;
        }
    }
    
    return false;
}

void VotingEngine::setAlarmCallback(AlarmCallback callback) {
    m_alarmCallback = callback;
}

bool VotingEngine::acknowledgeAlarm(uint32_t groupId) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            if (m_alarmFlags[i] != 0) {
                m_alarmFlags[i] = 0;
                header->activeAlarmCount.fetch_sub(1);
                return true;
            }
        }
    }
    
    return false;
}

uint32_t VotingEngine::getActiveAlarmCount() {
    if (!m_shm) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    return header->activeAlarmCount.load();
}

uint32_t VotingEngine::getActiveAlarms(uint32_t* groupIds, DeviationLevel* levels, 
                                        uint32_t maxCount) {
    if (!m_shm || !groupIds || !levels) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < header->groupCount && count < maxCount; i++) {
        if (m_alarmFlags[i] != 0) {
            groupIds[count] = m_configs[i].groupId;
            levels[count] = static_cast<DeviationLevel>(m_outputs[i].deviationLevel);
            count++;
        }
    }
    
    return count;
}

bool VotingEngine::checkHealth(uint32_t groupId, uint8_t& healthStatus, char* message) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            healthStatus = m_healthFlags[i];
            
            if (message) {
                if (m_outputs[i].validSourceCount < 3) {
                    strcpy(message, "Insufficient valid sources");
                } else if (m_outputs[i].deviationLevel > 0) {
                    strcpy(message, "Deviation detected");
                } else {
                    strcpy(message, "Healthy");
                }
            }
            
            return true;
        }
    }
    
    return false;
}

uint8_t VotingEngine::getValidSourceCount(uint32_t groupId) {
    if (!m_shm) return 0;
    
    for (uint32_t i = 0; i < static_cast<ShmHeader*>(m_shm)->groupCount; i++) {
        if (m_configs[i].groupId == groupId) {
            return m_outputs[i].validSourceCount;
        }
    }
    
    return 0;
}

} // namespace IPC
