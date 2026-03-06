#include "SOERecorder.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <vector>

namespace IPC {

// ========== 创建/连接/销毁 ==========

SOERecorder* SOERecorder::create(const char* name, uint32_t capacity) {
    if (!name || capacity == 0) {
        return nullptr;
    }
    
    SOERecorder* recorder = new SOERecorder();
    recorder->m_name = name;
    recorder->m_isCreator = true;
    
    // 计算需要的共享内存大小
    size_t requiredSize = recorder->calculateRequiredSize(capacity);
    
    // 创建共享内存 - 如果已存在则先清理
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd == -1) {
        // 共享内存可能已存在（上次异常退出），先删除再重试
        shm_unlink(name);
        fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd == -1) {
            delete recorder;
            return nullptr;
        }
    }
    
    // 设置大小
    if (ftruncate(fd, requiredSize) == -1) {
        ::close(fd);
        shm_unlink(name);
        delete recorder;
        return nullptr;
    }
    
    // 映射
    void* ptr = mmap(nullptr, requiredSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        shm_unlink(name);
        delete recorder;
        return nullptr;
    }
    
    recorder->m_shmFd = fd;
    recorder->m_shmPtr = ptr;
    recorder->m_shmSize = requiredSize;
    
    // 初始化头部
    recorder->m_header = static_cast<SOEBufferHeader*>(ptr);
    new (recorder->m_header) SOEBufferHeader();
    recorder->m_header->capacity = capacity;
    
    // 初始化记录数组
    recorder->m_records = reinterpret_cast<SOERecord*>(
        static_cast<char*>(ptr) + sizeof(SOEBufferHeader));
    
    // 初始化所有记录
    for (uint32_t i = 0; i < capacity; i++) {
        new (&recorder->m_records[i]) SOERecord();
    }
    
    return recorder;
}

SOERecorder* SOERecorder::connect(const char* name) {
    if (!name) {
        return nullptr;
    }
    
    SOERecorder* recorder = new SOERecorder();
    recorder->m_name = name;
    recorder->m_isCreator = false;
    
    // 打开已存在的共享内存
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd == -1) {
        delete recorder;
        return nullptr;
    }
    
    // 获取大小
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        ::close(fd);
        delete recorder;
        return nullptr;
    }
    
    // 映射
    void* ptr = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        delete recorder;
        return nullptr;
    }
    
    recorder->m_shmFd = fd;
    recorder->m_shmPtr = ptr;
    recorder->m_shmSize = sb.st_size;
    
    // 初始化指针
    if (!recorder->initFromShm()) {
        munmap(ptr, sb.st_size);
        ::close(fd);
        delete recorder;
        return nullptr;
    }
    
    return recorder;
}

void SOERecorder::destroy() {
    if (m_shmPtr) {
        // 清理记录
        if (m_records) {
            for (uint32_t i = 0; i < m_header->capacity; i++) {
                m_records[i].~SOERecord();
            }
        }
        
        // 清理头部
        if (m_header) {
            m_header->~SOEBufferHeader();
        }
        
        munmap(m_shmPtr, m_shmSize);
        m_shmPtr = nullptr;
    }
    
    if (m_shmFd != -1) {
        ::close(m_shmFd);
        m_shmFd = -1;
    }
    
    // 只有创建者才能删除共享内存
    if (m_isCreator && !m_name.empty()) {
        shm_unlink(m_name.c_str());
    }
    
    delete this;
}

void SOERecorder::disconnect() {
    if (m_shmPtr) {
        munmap(m_shmPtr, m_shmSize);
        m_shmPtr = nullptr;
    }
    
    if (m_shmFd != -1) {
        ::close(m_shmFd);
        m_shmFd = -1;
    }
    
    m_header = nullptr;
    m_records = nullptr;
    
    delete this;
}

bool SOERecorder::initFromShm() {
    if (!m_shmPtr) return false;
    
    m_header = static_cast<SOEBufferHeader*>(m_shmPtr);
    
    // 验证魔数
    if (m_header->magic != SOE_MAGIC) {
        return false;
    }
    
    // 验证版本
    if (m_header->version != SOE_VERSION) {
        return false;
    }
    
    // 计算记录数组位置
    m_records = reinterpret_cast<SOERecord*>(
        static_cast<char*>(m_shmPtr) + sizeof(SOEBufferHeader));
    
    return true;
}

size_t SOERecorder::calculateRequiredSize(uint32_t capacity) {
    return sizeof(SOEBufferHeader) + capacity * sizeof(SOERecord);
}

// ========== 记录操作 ==========

Result SOERecorder::record(const SOERecord& record) {
    if (!isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    lockWrite();
    
    uint32_t head = m_header->head.load();
    uint32_t tail = m_header->tail.load();
    uint32_t next = (head + 1) % m_header->capacity;
    
    // 检查是否满
    if (next == tail) {
        // 缓冲区满，丢弃最旧的记录
        m_header->tail.store((tail + 1) % m_header->capacity);
        m_header->droppedRecords.fetch_add(1);
    }
    
    // 写入记录
    m_records[head] = record;
    m_header->head.store(next);
    m_header->totalRecords.fetch_add(1);
    m_header->lastRecordTime.store(record.absoluteTime);
    
    unlockWrite();
    
    return Result::OK;
}

Result SOERecorder::recordYXChange(uint32_t pointKey, uint8_t oldValue, 
                                    uint8_t newValue, uint8_t priority) {
    SOERecord rec;
    rec.absoluteTime = getAbsoluteTimeNs();
    rec.monotonicTime = getMonotonicTimeNs();
    rec.pointKey = pointKey;
    rec.msoc = getMsoc();
    rec.sourcePid = getpid();
    rec.pointType = static_cast<uint8_t>(PointType::YX);
    rec.eventType = static_cast<uint8_t>(SOEEventType::YX_CHANGE);
    rec.quality = static_cast<uint8_t>(SOEQuality::VALID);
    rec.oldValue = oldValue;
    rec.newValue = newValue;
    rec.priority = priority;
    
    return record(rec);
}

Result SOERecorder::recordYKExecute(uint32_t pointKey, uint8_t command, 
                                     uint8_t priority) {
    SOERecord rec;
    rec.absoluteTime = getAbsoluteTimeNs();
    rec.monotonicTime = getMonotonicTimeNs();
    rec.pointKey = pointKey;
    rec.msoc = getMsoc();
    rec.sourcePid = getpid();
    rec.pointType = static_cast<uint8_t>(PointType::YK);
    rec.eventType = static_cast<uint8_t>(SOEEventType::YK_EXECUTE);
    rec.quality = static_cast<uint8_t>(SOEQuality::VALID);
    rec.oldValue = 0;
    rec.newValue = command;
    rec.priority = priority;
    
    return record(rec);
}

Result SOERecorder::recordProtectionAct(uint32_t pointKey, uint8_t action,
                                         uint8_t priority) {
    SOERecord rec;
    rec.absoluteTime = getAbsoluteTimeNs();
    rec.monotonicTime = getMonotonicTimeNs();
    rec.pointKey = pointKey;
    rec.msoc = getMsoc();
    rec.sourcePid = getpid();
    rec.pointType = static_cast<uint8_t>(PointType::YX);
    rec.eventType = static_cast<uint8_t>(SOEEventType::PROTECTION_ACT);
    rec.quality = static_cast<uint8_t>(SOEQuality::VALID);
    rec.oldValue = 0;
    rec.newValue = action;
    rec.priority = priority;
    
    return record(rec);
}

// ========== 查询操作 ==========

Result SOERecorder::query(const SOEQueryCondition& condition,
                           SOERecord* records, uint32_t& count, uint32_t maxCount) {
    if (!isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    if (!records || maxCount == 0) {
        return Result::INVALID_PARAM;
    }
    
    lockRead();
    
    uint32_t head = m_header->head.load();
    uint32_t tail = m_header->tail.load();
    
    std::vector<SOERecord> tempRecords;
    tempRecords.reserve(maxCount);
    
    // 遍历所有记录
    uint32_t current = tail;
    while (current != head) {
        const SOERecord& rec = m_records[current];
        
        // 应用过滤条件
        bool match = true;
        
        if (condition.startTime != 0 && rec.absoluteTime < condition.startTime) {
            match = false;
        }
        
        if (condition.endTime != 0 && rec.absoluteTime > condition.endTime) {
            match = false;
        }
        
        if (condition.pointKey != 0 && rec.pointKey != condition.pointKey) {
            match = false;
        }
        
        if (condition.pointType != 0xFF && rec.pointType != condition.pointType) {
            match = false;
        }
        
        if (condition.eventType != 0xFF && rec.eventType != condition.eventType) {
            match = false;
        }
        
        if (rec.priority < condition.minPriority) {
            match = false;
        }
        
        if (match) {
            tempRecords.push_back(rec);
            
            if (tempRecords.size() >= maxCount) {
                break;
            }
        }
        
        current = (current + 1) % m_header->capacity;
    }
    
    unlockRead();
    
    // 排序
    if (condition.reverseOrder) {
        std::reverse(tempRecords.begin(), tempRecords.end());
    }
    
    // 复制结果
    count = static_cast<uint32_t>(tempRecords.size());
    for (uint32_t i = 0; i < count; i++) {
        records[i] = tempRecords[i];
    }
    
    return Result::OK;
}

Result SOERecorder::getLatest(uint32_t count, SOERecord* records, uint32_t& actualCount) {
    if (!isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    lockRead();
    
    uint32_t head = m_header->head.load();
    uint32_t tail = m_header->tail.load();
    
    // 计算可用记录数
    uint32_t available = (head >= tail) ? (head - tail) : (m_header->capacity - tail + head);
    actualCount = std::min(count, available);
    
    // 从最新开始复制
    for (uint32_t i = 0; i < actualCount; i++) {
        uint32_t index = (head + m_header->capacity - 1 - i) % m_header->capacity;
        records[i] = m_records[index];
    }
    
    unlockRead();
    
    return Result::OK;
}

Result SOERecorder::getByTimeRange(uint64_t startTime, uint64_t endTime,
                                    SOERecord* records, uint32_t& count, uint32_t maxCount) {
    SOEQueryCondition condition;
    condition.startTime = startTime;
    condition.endTime = endTime;
    condition.maxRecords = maxCount;
    condition.reverseOrder = false;  // 时间正序
    
    return query(condition, records, count, maxCount);
}

// ========== 统计与监控 ==========

SOEStats SOERecorder::getStats() const {
    SOEStats stats;
    
    if (!isValid()) {
        return stats;
    }
    
    stats.totalRecords = m_header->totalRecords.load();
    stats.droppedRecords = m_header->droppedRecords.load();
    stats.lastRecordTime = m_header->lastRecordTime.load();
    stats.capacity = m_header->capacity;
    
    uint32_t head = m_header->head.load();
    uint32_t tail = m_header->tail.load();
    
    if (head >= tail) {
        stats.currentLoad = head - tail;
    } else {
        stats.currentLoad = m_header->capacity - tail + head;
    }
    
    if (stats.capacity > 0) {
        stats.loadPercent = static_cast<float>(stats.currentLoad) / stats.capacity * 100.0f;
    }
    
    return stats;
}

void SOERecorder::clear() {
    if (!isValid()) {
        return;
    }
    
    lockWrite();
    
    m_header->head.store(0);
    m_header->tail.store(0);
    
    unlockWrite();
}

// ========== 导出功能 ==========

Result SOERecorder::exportToCSV(const char* filename, const SOEQueryCondition* condition) {
    if (!filename) {
        return Result::INVALID_PARAM;
    }
    
    // 获取所有记录（带条件）
    SOEQueryCondition defaultCondition;
    if (condition) {
        defaultCondition = *condition;
    }
    defaultCondition.maxRecords = MAX_SOE_RECORDS;
    defaultCondition.reverseOrder = true;
    
    std::vector<SOERecord> records;
    records.resize(MAX_SOE_RECORDS);
    uint32_t count = 0;
    
    Result result = query(defaultCondition, records.data(), count, MAX_SOE_RECORDS);
    if (result != Result::OK) {
        return result;
    }
    
    // 写入CSV
    std::ofstream file(filename);
    if (!file.is_open()) {
        return Result::ERROR;
    }
    
    // 表头
    file << "时间戳,时间字符串,点号,类型,事件类型,旧值,新值,质量,优先级,来源PID\n";
    
    // 数据
    for (uint32_t i = 0; i < count; i++) {
        const SOERecord& rec = records[i];
        file << rec.absoluteTime << ","
             << timeNsToString(rec.absoluteTime) << ","
             << rec.pointKey << ","
             << static_cast<int>(rec.pointType) << ","
             << static_cast<int>(rec.eventType) << ","
             << static_cast<int>(rec.oldValue) << ","
             << static_cast<int>(rec.newValue) << ","
             << static_cast<int>(rec.quality) << ","
             << static_cast<int>(rec.priority) << ","
             << rec.sourcePid << "\n";
    }
    
    file.close();
    
    return Result::OK;
}

Result SOERecorder::exportToCOMTRADE(const char* filename, uint64_t triggerTime,
                                      uint32_t preTime, uint32_t postTime) {
    if (!filename) {
        return Result::INVALID_PARAM;
    }
    
    // 计算时间范围（纳秒）
    uint64_t startTime = triggerTime - static_cast<uint64_t>(preTime) * 1000000ULL;
    uint64_t endTime = triggerTime + static_cast<uint64_t>(postTime) * 1000000ULL;
    
    // 获取记录
    std::vector<SOERecord> records;
    records.resize(MAX_SOE_RECORDS);
    uint32_t count = 0;
    
    Result result = getByTimeRange(startTime, endTime, records.data(), count, MAX_SOE_RECORDS);
    if (result != Result::OK) {
        return result;
    }
    
    // 生成配置文件 (.cfg)
    std::string cfgFilename = std::string(filename) + ".cfg";
    std::ofstream cfgFile(cfgFilename);
    if (!cfgFile.is_open()) {
        return Result::ERROR;
    }
    
    // COMTRADE 配置文件格式
    cfgFile << "# SOE Event Export\n";
    cfgFile << "# Generated by IPCSharedDataPool\n";
    cfgFile << count << ",1A,0D\n";  // 模拟通道数，数字通道数
    cfgFile << "1,EVENT,0\n";        // 通道定义
    
    // 时间信息
    time_t seconds = triggerTime / 1000000000ULL;
    struct tm* tm_info = localtime(&seconds);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y,%H:%M:%S", tm_info);
    cfgFile << timeStr << "\n";
    
    cfgFile.close();
    
    // 生成数据文件 (.dat)
    std::string datFilename = std::string(filename) + ".dat";
    std::ofstream datFile(datFilename, std::ios::binary);
    if (!datFile.is_open()) {
        return Result::ERROR;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        const SOERecord& rec = records[i];
        // 写入记录数据
        datFile.write(reinterpret_cast<const char*>(&rec), sizeof(SOERecord));
    }
    
    datFile.close();
    
    return Result::OK;
}

} // namespace IPC
