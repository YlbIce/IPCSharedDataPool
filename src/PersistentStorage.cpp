#include "PersistentStorage.h"
#include "SOERecorder.h"  // for getAbsoluteTimeNs
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace IPC {

// ========== 构造/析构 ==========

PersistentStorage::PersistentStorage(SharedDataPool* dataPool, const PersistentConfig& config)
    : m_dataPool(dataPool)
    , m_config(config)
    , m_autoSnapshot(config.enableAutoSnapshot)
    , m_lastSnapshotTime(0)
    , m_running(false) {
    
    if (m_autoSnapshot.load()) {
        m_running.store(true);
        m_snapshotThread = std::thread(&PersistentStorage::snapshotThread, this);
    }
}

PersistentStorage::~PersistentStorage() {
    m_running.store(false);
    m_cv.notify_all();
    
    if (m_snapshotThread.joinable()) {
        m_snapshotThread.join();
    }
}

// ========== 初始化与恢复 ==========

Result PersistentStorage::initialize() {
    if (!m_dataPool || !m_dataPool->isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    // 按类型初始化
    Result result;
    
    result = initializeYX();
    if (result != Result::OK) {
        return result;
    }
    
    result = initializeYC();
    if (result != Result::OK) {
        return result;
    }
    
    result = initializeDZ();
    if (result != Result::OK) {
        return result;
    }
    
    result = initializeYK();
    if (result != Result::OK) {
        return result;
    }
    
    return Result::OK;
}

Result PersistentStorage::restore(const char* filename) {
    if (!m_dataPool || !m_dataPool->isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    const char* path = filename ? filename : m_config.snapshotPath;
    
    // 检查文件是否存在
    if (!fs::exists(path)) {
        return Result::NOT_FOUND;
    }
    
    // 尝试从快照文件加载
    Result result = readSnapshotFile(path);
    if (result != Result::OK) {
        // 尝试从备份恢复
        result = restoreFromBackup(0);
    }
    
    return result;
}

Result PersistentStorage::restoreDefaults(PointType type) {
    if (!m_dataPool || !m_dataPool->isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    uint32_t count = 0;
    
    switch (type) {
        case PointType::YX:
            count = m_dataPool->getYXCount();
            for (uint32_t i = 0; i < count; i++) {
                // 设置默认值：分合闸状态通常为0（分位）
                m_dataPool->setYXByIndex(i, 0, getCurrentTimestamp(), 0);
            }
            break;
            
        case PointType::YC:
            count = m_dataPool->getYCCount();
            for (uint32_t i = 0; i < count; i++) {
                // 设置默认值：0.0
                m_dataPool->setYCByIndex(i, 0.0f, getCurrentTimestamp(), 0);
            }
            break;
            
        case PointType::DZ:
            count = m_dataPool->getDZCount();
            for (uint32_t i = 0; i < count; i++) {
                m_dataPool->setDZ(0, 0.0f, getCurrentTimestamp(), 0);
            }
            break;
            
        case PointType::YK:
            count = m_dataPool->getYKCount();
            for (uint32_t i = 0; i < count; i++) {
                m_dataPool->setYK(0, 0, getCurrentTimestamp(), 0);
            }
            break;
    }
    
    return Result::OK;
}

Result PersistentStorage::initializeYX() {
    switch (m_config.yxInitMode) {
        case InitMode::LOAD_LAST_VALUE:
            // 尝试从快照加载
            if (hasValidSnapshot()) {
                return restore();
            }
            // 如果没有快照，使用默认值
            return restoreDefaults(PointType::YX);
            
        case InitMode::LOAD_DEFAULT:
            return restoreDefaults(PointType::YX);
            
        case InitMode::WAIT_FOR_FRESH:
            // 设置质量码为无效，等待新数据
            // 实际应用中需要配合质量码机制
            return Result::OK;
            
        case InitMode::INVALIDATE:
            // 设置为无效状态
            return Result::OK;
            
        default:
            return Result::INVALID_PARAM;
    }
}

Result PersistentStorage::initializeYC() {
    switch (m_config.ycInitMode) {
        case InitMode::LOAD_LAST_VALUE:
            if (hasValidSnapshot()) {
                return restore();
            }
            return restoreDefaults(PointType::YC);
            
        case InitMode::LOAD_DEFAULT:
            return restoreDefaults(PointType::YC);
            
        case InitMode::WAIT_FOR_FRESH:
            return Result::OK;
            
        case InitMode::INVALIDATE:
            return Result::OK;
            
        default:
            return Result::INVALID_PARAM;
    }
}

Result PersistentStorage::initializeDZ() {
    switch (m_config.dzInitMode) {
        case InitMode::LOAD_LAST_VALUE:
            if (hasValidSnapshot()) {
                return restore();
            }
            return restoreDefaults(PointType::DZ);
            
        case InitMode::LOAD_DEFAULT:
            return restoreDefaults(PointType::DZ);
            
        case InitMode::WAIT_FOR_FRESH:
            return Result::OK;
            
        case InitMode::INVALIDATE:
            return Result::OK;
            
        default:
            return Result::INVALID_PARAM;
    }
}

Result PersistentStorage::initializeYK() {
    switch (m_config.ykInitMode) {
        case InitMode::LOAD_LAST_VALUE:
            if (hasValidSnapshot()) {
                return restore();
            }
            return restoreDefaults(PointType::YK);
            
        case InitMode::LOAD_DEFAULT:
            return restoreDefaults(PointType::YK);
            
        case InitMode::WAIT_FOR_FRESH:
            return Result::OK;
            
        case InitMode::INVALIDATE:
            // 遥控通常不需要保持状态
            return Result::OK;
            
        default:
            return Result::INVALID_PARAM;
    }
}

// ========== 快照操作 ==========

Result PersistentStorage::saveSnapshot(const char* filename) {
    if (!m_dataPool || !m_dataPool->isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    const char* path = filename ? filename : m_config.snapshotPath;
    
    return writeSnapshotFile(path);
}

void PersistentStorage::enableAutoSnapshot(bool enable) {
    bool wasEnabled = m_autoSnapshot.exchange(enable);
    
    if (!wasEnabled && enable) {
        // 启动快照线程
        if (!m_running.load()) {
            m_running.store(true);
            if (!m_snapshotThread.joinable()) {
                m_snapshotThread = std::thread(&PersistentStorage::snapshotThread, this);
            }
        }
    } else if (wasEnabled && !enable) {
        // 停止快照线程
        m_running.store(false);
        m_cv.notify_all();
    }
}

void PersistentStorage::setSnapshotInterval(uint32_t intervalMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.snapshotIntervalMs = intervalMs;
}

Result PersistentStorage::triggerSnapshot() {
    Result result = saveSnapshot();
    if (result == Result::OK) {
        m_lastSnapshotTime.store(getCurrentTimestamp());
    }
    return result;
}

// ========== 备份管理 ==========

Result PersistentStorage::createBackup() {
    if (!m_dataPool || !m_dataPool->isValid()) {
        return Result::NOT_INITIALIZED;
    }
    
    std::string backupFile = generateBackupFilename();
    return writeSnapshotFile(backupFile.c_str());
}

Result PersistentStorage::restoreFromBackup(uint32_t backupIndex) {
    auto backupList = getBackupList();
    
    if (backupIndex >= backupList.size()) {
        return Result::NOT_FOUND;
    }
    
    // 备份列表已按时间排序（最新在前）
    return readSnapshotFile(backupList[backupIndex].c_str());
}

std::vector<std::string> PersistentStorage::getBackupList() const {
    std::vector<std::string> backups;
    
    std::string backupDir = m_config.backupPath;
    if (!fs::exists(backupDir)) {
        return backups;
    }
    
    // 遍历目录，找到所有备份文件
    for (const auto& entry : fs::directory_iterator(backupDir)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            // 匹配备份文件模式
            if (filename.find("backup_") == 0 && 
                filename.find(".snapshot") != std::string::npos) {
                backups.push_back(entry.path().string());
            }
        }
    }
    
    // 按修改时间排序（最新在前）
    std::sort(backups.begin(), backups.end(), [](const std::string& a, const std::string& b) {
        return fs::last_write_time(a) > fs::last_write_time(b);
    });
    
    return backups;
}

void PersistentStorage::cleanupOldBackups(uint32_t keepCount) {
    auto backupList = getBackupList();
    
    // 删除超出保留数量的备份
    for (size_t i = keepCount; i < backupList.size(); i++) {
        fs::remove(backupList[i]);
    }
}

// ========== 状态查询 ==========

bool PersistentStorage::hasValidSnapshot() const {
    std::string path = m_config.snapshotPath;
    
    if (!fs::exists(path)) {
        return false;
    }
    
    // 验证文件格式
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    PersistHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    file.close();
    
    return header.magic == PERSIST_MAGIC;
}

bool PersistentStorage::getSnapshotInfo(const char* filename, PersistHeader& header) const {
    if (!filename) {
        return false;
    }
    
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    file.close();
    
    return header.magic == PERSIST_MAGIC;
}

void PersistentStorage::updateConfig(const PersistentConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
    m_autoSnapshot.store(config.enableAutoSnapshot);
}

// ========== 点位初始化配置 ==========

void PersistentStorage::setPointInitConfig(const PointInitConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 查找是否已存在
    for (auto& pc : m_pointConfigs) {
        if (pc.key == config.key) {
            pc = config;
            return;
        }
    }
    
    // 添加新配置
    m_pointConfigs.push_back(config);
}

void PersistentStorage::setPointInitConfigs(const PointInitConfig* configs, uint32_t count) {
    if (!configs) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pointConfigs.clear();
    m_pointConfigs.reserve(count);
    
    for (uint32_t i = 0; i < count; i++) {
        m_pointConfigs.push_back(configs[i]);
    }
}

bool PersistentStorage::getPointInitConfig(uint64_t key, PointInitConfig& config) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    
    for (const auto& pc : m_pointConfigs) {
        if (pc.key == key) {
            config = pc;
            return true;
        }
    }
    
    return false;
}

// ========== 内部方法 ==========

void PersistentStorage::snapshotThread() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        // 等待超时或停止信号
        if (m_cv.wait_for(lock, 
                          std::chrono::milliseconds(m_config.snapshotIntervalMs),
                          [this] { return !m_running.load(); })) {
            break;  // 收到停止信号
        }
        
        lock.unlock();
        
        // 执行快照
        if (m_autoSnapshot.load()) {
            Result result = saveSnapshot();
            if (result == Result::OK) {
                m_lastSnapshotTime.store(getCurrentTimestamp());
            }
        }
    }
}

Result PersistentStorage::writeSnapshotFile(const char* filename) {
    // 确保目录存在
    fs::path filePath(filename);
    fs::path dirPath = filePath.parent_path();
    if (!dirPath.empty() && !fs::exists(dirPath)) {
        fs::create_directories(dirPath);
    }
    
    // 加锁读取数据
    m_dataPool->lockRead();
    
    // 准备头部
    PersistHeader header;
    header.magic = PERSIST_MAGIC;
    header.version = PERSIST_VERSION;
    header.snapshotTime = getAbsoluteTimeNs();
    header.shmSize = m_dataPool->m_shmSize;
    
    const ShmHeader* shmHeader = m_dataPool->getHeader();
    header.yxCount = shmHeader->yxCount;
    header.ycCount = shmHeader->ycCount;
    header.dzCount = shmHeader->dzCount;
    header.ykCount = shmHeader->ykCount;
    header.indexCount = shmHeader->indexCount;
    
    // 数据区大小 = 共享内存总大小 - 头部大小
    size_t dataSize = m_dataPool->m_shmSize - sizeof(ShmHeader);
    
    // 计算校验和
    header.checksum = calculateChecksum(
        static_cast<const char*>(m_dataPool->m_shmPtr) + sizeof(ShmHeader),
        dataSize);
    
    // 写入文件
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        m_dataPool->unlockRead();
        return Result::ERROR;
    }
    
    // 写入头部
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // 写入数据区（从ShmHeader之后到共享内存末尾）
    const char* dataStart = static_cast<const char*>(m_dataPool->m_shmPtr) + sizeof(ShmHeader);
    file.write(dataStart, dataSize);
    
    file.close();
    m_dataPool->unlockRead();
    
    return Result::OK;
}

Result PersistentStorage::readSnapshotFile(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return Result::NOT_FOUND;
    }
    
    // 读取头部
    PersistHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // 验证
    if (header.magic != PERSIST_MAGIC) {
        file.close();
        return Result::ERROR;
    }
    
    if (header.version != PERSIST_VERSION) {
        file.close();
        return Result::ERROR;
    }
    
    // 验证数据池大小是否匹配
    const ShmHeader* shmHeader = m_dataPool->getHeader();
    if (header.yxCount != shmHeader->yxCount ||
        header.ycCount != shmHeader->ycCount ||
        header.dzCount != shmHeader->dzCount ||
        header.ykCount != shmHeader->ykCount) {
        file.close();
        return Result::ERROR;
    }
    
    // 数据区大小 = 共享内存总大小 - 头部大小
    size_t dataSize = m_dataPool->m_shmSize - sizeof(ShmHeader);
    
    // 读取数据
    std::vector<char> buffer(dataSize);
    file.read(buffer.data(), dataSize);
    file.close();
    
    // 验证校验和
    uint32_t checksum = calculateChecksum(buffer.data(), dataSize);
    if (checksum != header.checksum) {
        return Result::ERROR;
    }
    
    // 加锁写入数据
    m_dataPool->lockWrite();
    
    char* dataStart = static_cast<char*>(m_dataPool->m_shmPtr) + sizeof(ShmHeader);
    std::memcpy(dataStart, buffer.data(), dataSize);
    
    m_dataPool->unlockWrite();
    
    return Result::OK;
}

std::string PersistentStorage::generateBackupFilename() {
    // 确保备份目录存在
    if (!fs::exists(m_config.backupPath)) {
        fs::create_directories(m_config.backupPath);
    }
    
    // 生成带时间戳的文件名（精确到纳秒确保唯一性）
    uint64_t now = getAbsoluteTimeNs();
    time_t seconds = now / 1000000000ULL;
    uint32_t nanos = now % 1000000000ULL;
    struct tm* tm_info = localtime(&seconds);
    
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", tm_info);
    
    // 添加纳秒部分确保唯一性
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/backup_%s_%09u.snapshot", 
             m_config.backupPath, buffer, nanos);
    
    return std::string(filename);
}

uint32_t PersistentStorage::calculateChecksum(const void* data, size_t len) const {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;
    
    // 简单的累加校验
    for (size_t i = 0; i < len; i++) {
        sum += ptr[i];
        // 加入位置因子增强校验强度
        sum ^= (static_cast<uint32_t>(i) << 8);
    }
    
    return sum;
}

} // namespace IPC
