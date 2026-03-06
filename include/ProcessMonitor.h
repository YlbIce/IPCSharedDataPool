/**
 * @file ProcessMonitor.h
 * @brief 进程监控工具 - 获取CPU、内存等系统资源使用情况
 * 
 * 功能：
 * - 获取当前进程的CPU使用率
 * - 获取当前进程的内存使用量
 * - 获取系统总体资源使用情况
 * - 获取共享内存使用情况
 */

#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>

namespace IPC {

/**
 * @brief 进程资源使用信息
 */
struct ProcessResourceInfo {
    // 进程信息
    pid_t pid;                  ///< 进程ID
    char name[64];              ///< 进程名称
    
    // CPU 信息
    double cpuPercent;          ///< CPU使用率 (0-100% * CPU核心数)
    double cpuPercentPerCore;   ///< 单核CPU使用率 (0-100%)
    uint64_t userTime;          ///< 用户态时间 (毫秒)
    uint64_t systemTime;        ///< 内核态时间 (毫秒)
    uint64_t totalTime;         ///< 总CPU时间 (毫秒)
    
    // 内存信息
    uint64_t vmSize;            ///< 虚拟内存大小 (KB)
    uint64_t vmRSS;             ///< 物理内存使用 (KB)
    uint64_t vmData;            ///< 数据段大小 (KB)
    uint64_t vmStk;             ///< 栈大小 (KB)
    double memoryPercent;       ///< 内存使用率 (0-100%)
    
    // 线程信息
    uint32_t threads;           ///< 线程数
    
    // 文件描述符
    uint32_t fdCount;           ///< 打开的文件描述符数
    
    // 时间戳
    uint64_t timestamp;         ///< 采样时间戳 (毫秒)
    
    ProcessResourceInfo() 
        : pid(0), cpuPercent(0), cpuPercentPerCore(0)
        , userTime(0), systemTime(0), totalTime(0)
        , vmSize(0), vmRSS(0), vmData(0), vmStk(0), memoryPercent(0)
        , threads(0), fdCount(0), timestamp(0) {
        std::memset(name, 0, sizeof(name));
    }
};

/**
 * @brief 系统资源使用信息
 */
struct SystemResourceInfo {
    // CPU 信息
    uint32_t cpuCores;          ///< CPU核心数
    double cpuUsage;            ///< 系统CPU使用率 (0-100%)
    double loadAvg1;            ///< 1分钟负载
    double loadAvg5;            ///< 5分钟负载
    double loadAvg15;           ///< 15分钟负载
    
    // 内存信息
    uint64_t totalMem;          ///< 总内存 (KB)
    uint64_t freeMem;           ///< 空闲内存 (KB)
    uint64_t availableMem;      ///< 可用内存 (KB)
    uint64_t usedMem;           ///< 已用内存 (KB)
    double memoryUsage;         ///< 内存使用率 (0-100%)
    
    // 交换分区
    uint64_t totalSwap;         ///< 总交换空间 (KB)
    uint64_t usedSwap;          ///< 已用交换空间 (KB)
    
    // 运行时间
    uint64_t uptime;            ///< 系统运行时间 (秒)
    uint32_t processCount;      ///< 进程数
    
    // 时间戳
    uint64_t timestamp;         ///< 采样时间戳 (毫秒)
    
    SystemResourceInfo()
        : cpuCores(0), cpuUsage(0), loadAvg1(0), loadAvg5(0), loadAvg15(0)
        , totalMem(0), freeMem(0), availableMem(0), usedMem(0), memoryUsage(0)
        , totalSwap(0), usedSwap(0), uptime(0), processCount(0), timestamp(0) {}
};

/**
 * @brief 共享内存使用信息
 */
struct ShmUsageInfo {
    std::string name;           ///< 共享内存名称
    uint64_t size;              ///< 大小 (字节)
    uint64_t usedBytes;         ///< 已用字节
    double usagePercent;        ///< 使用率
    
    ShmUsageInfo() : size(0), usedBytes(0), usagePercent(0) {}
};

/**
 * @brief 进程监控类
 * 
 * 用于获取进程和系统的资源使用情况
 * 
 * 使用示例：
 * @code
 * ProcessMonitor monitor;
 * 
 * // 获取当前进程信息
 * ProcessResourceInfo procInfo;
 * monitor.getProcessInfo(procInfo);
 * printf("CPU: %.1f%%, Memory: %lu KB\n", procInfo.cpuPercent, procInfo.vmRSS);
 * 
 * // 获取系统信息
 * SystemResourceInfo sysInfo;
 * monitor.getSystemInfo(sysInfo);
 * printf("System CPU: %.1f%%, Memory: %.1f%%\n", sysInfo.cpuUsage, sysInfo.memoryUsage);
 * @endcode
 */
class ProcessMonitor {
public:
    ProcessMonitor() 
        : m_lastTotalTime(0)
        , m_lastUserTime(0)
        , m_lastSystemTime(0)
        , m_lastTimestamp(0)
        , m_lastSysTotal(0)
        , m_lastSysIdle(0)
        , m_lastSysTimestamp(0) {
        m_pid = getpid();
        updateProcessName();
    }
    
    explicit ProcessMonitor(pid_t pid)
        : m_pid(pid)
        , m_lastTotalTime(0)
        , m_lastUserTime(0)
        , m_lastSystemTime(0)
        , m_lastTimestamp(0)
        , m_lastSysTotal(0)
        , m_lastSysIdle(0)
        , m_lastSysTimestamp(0) {
        updateProcessName();
    }
    
    /**
     * @brief 获取进程资源使用信息
     * @param info 输出参数，进程信息
     * @return 成功返回true
     */
    bool getProcessInfo(ProcessResourceInfo& info) {
        info.pid = m_pid;
        std::strncpy(info.name, m_name.c_str(), sizeof(info.name) - 1);
        info.timestamp = getCurrentTimestamp();
        
        // 读取 /proc/[pid]/stat
        if (!readProcessStat(info)) {
            return false;
        }
        
        // 读取 /proc/[pid]/status 获取内存信息
        readProcessStatus(info);
        
        // 获取文件描述符数量
        info.fdCount = getFdCount();
        
        // 计算CPU使用率
        calculateCpuPercent(info);
        
        return true;
    }
    
    /**
     * @brief 获取系统资源使用信息
     * @param info 输出参数，系统信息
     * @return 成功返回true
     */
    bool getSystemInfo(SystemResourceInfo& info) {
        info.timestamp = getCurrentTimestamp();
        info.cpuCores = sysconf(_SC_NPROCESSORS_ONLN);
        
        // 读取 /proc/meminfo
        readMemInfo(info);
        
        // 读取 /proc/loadavg
        readLoadAvg(info);
        
        // 读取 /proc/stat 计算CPU使用率
        calculateSystemCpu(info);
        
        // 读取系统运行时间和进程数
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            info.uptime = si.uptime;
            info.processCount = si.procs;
        }
        
        return true;
    }
    
    /**
     * @brief 获取共享内存信息
     * @param name 共享内存名称
     * @param totalSize 总大小
     * @param usedSize 已用大小
     * @return 共享内存使用信息
     */
    static ShmUsageInfo getShmInfo(const std::string& name, uint64_t totalSize, uint64_t usedSize) {
        ShmUsageInfo info;
        info.name = name;
        info.size = totalSize;
        info.usedBytes = usedSize;
        info.usagePercent = totalSize > 0 ? (usedSize * 100.0 / totalSize) : 0;
        return info;
    }
    
    /**
     * @brief 格式化内存大小为可读字符串
     * @param kb 大小（KB）
     * @return 格式化字符串，如 "1.5 GB", "256 MB", "128 KB"
     */
    static std::string formatMemory(uint64_t kb) {
        const char* units[] = {"KB", "MB", "GB", "TB"};
        int unitIdx = 0;
        double value = static_cast<double>(kb);
        
        while (value >= 1024 && unitIdx < 3) {
            value /= 1024;
            unitIdx++;
        }
        
        char buf[64];
        if (unitIdx > 0) {
            snprintf(buf, sizeof(buf), "%.1f %s", value, units[unitIdx]);
        } else {
            snprintf(buf, sizeof(buf), "%.0f %s", value, units[unitIdx]);
        }
        return std::string(buf);
    }
    
    /**
     * @brief 获取进程名
     */
    const std::string& getName() const { return m_name; }
    
    /**
     * @brief 获取PID
     */
    pid_t getPid() const { return m_pid; }

private:
    pid_t m_pid;
    std::string m_name;
    
    // 上次采样的CPU时间，用于计算使用率
    uint64_t m_lastTotalTime;
    uint64_t m_lastUserTime;
    uint64_t m_lastSystemTime;
    uint64_t m_lastTimestamp;
    
    // 系统CPU计算
    uint64_t m_lastSysTotal;
    uint64_t m_lastSysIdle;
    uint64_t m_lastSysTimestamp;
    
    void updateProcessName() {
        // 读取进程名
        std::ifstream commFile("/proc/" + std::to_string(m_pid) + "/comm");
        if (commFile.is_open()) {
            std::getline(commFile, m_name);
            // 移除换行符
            while (!m_name.empty() && (m_name.back() == '\n' || m_name.back() == '\r')) {
                m_name.pop_back();
            }
        } else {
            m_name = "unknown";
        }
    }
    
    bool readProcessStat(ProcessResourceInfo& info) {
        std::ifstream statFile("/proc/" + std::to_string(m_pid) + "/stat");
        if (!statFile.is_open()) {
            return false;
        }
        
        std::string line;
        std::getline(statFile, line);
        
        // 解析 /proc/[pid]/stat
        // 格式: pid (comm) state ppid pgrp session tty_nr tpgid flags ...
        // 我们需要: utime(14), stime(15), num_threads(20)
        
        // 找到 comm 后面的 )
        size_t pos = line.rfind(')');
        if (pos == std::string::npos) {
            return false;
        }
        
        // 从 ) 后面开始解析
        std::istringstream iss(line.substr(pos + 2));
        std::string token;
        std::vector<std::string> tokens;
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        // tokens[0] = state, tokens[1] = ppid, ...
        // utime 是第14个字段，从 ) 后是第3个 (索引2)
        // stime 是第15个字段 (索引3)
        // num_threads 是第20个字段 (索引18)
        
        if (tokens.size() < 19) {
            return false;
        }
        
        // 获取时钟频率
        long clkTck = sysconf(_SC_CLK_TCK);
        
        // utime 和 stime 单位是时钟周期，转换为毫秒
        info.userTime = std::stoull(tokens[11]) * 1000 / clkTck;
        info.systemTime = std::stoull(tokens[12]) * 1000 / clkTck;
        info.totalTime = info.userTime + info.systemTime;
        info.threads = std::stoul(tokens[17]);
        
        return true;
    }
    
    void readProcessStatus(ProcessResourceInfo& info) {
        std::ifstream statusFile("/proc/" + std::to_string(m_pid) + "/status");
        if (!statusFile.is_open()) {
            return;
        }
        
        std::string line;
        while (std::getline(statusFile, line)) {
            if (line.compare(0, 7, "VmSize:") == 0) {
                sscanf(line.c_str(), "VmSize: %lu kB", &info.vmSize);
            } else if (line.compare(0, 6, "VmRSS:") == 0) {
                sscanf(line.c_str(), "VmRSS: %lu kB", &info.vmRSS);
            } else if (line.compare(0, 7, "VmData:") == 0) {
                sscanf(line.c_str(), "VmData: %lu kB", &info.vmData);
            } else if (line.compare(0, 6, "VmStk:") == 0) {
                sscanf(line.c_str(), "VmStk: %lu kB", &info.vmStk);
            }
        }
        
        // 计算内存使用率
        SystemResourceInfo sysInfo;
        readMemInfo(sysInfo);
        if (sysInfo.totalMem > 0) {
            info.memoryPercent = info.vmRSS * 100.0 / sysInfo.totalMem;
        }
    }
    
    uint32_t getFdCount() {
        std::string fdPath = "/proc/" + std::to_string(m_pid) + "/fd";
        DIR* dir = opendir(fdPath.c_str());
        if (!dir) {
            return 0;
        }
        
        uint32_t count = 0;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_LNK) {
                count++;
            }
        }
        closedir(dir);
        return count;
    }
    
    void calculateCpuPercent(ProcessResourceInfo& info) {
        uint64_t now = getCurrentTimestamp();
        uint64_t timeDiff = now - m_lastTimestamp;
        
        if (timeDiff > 0 && m_lastTimestamp > 0) {
            // 计算时间差内的CPU使用时间
            uint64_t cpuTimeDiff = info.totalTime - m_lastTotalTime;
            
            // CPU使用率 = CPU时间 / 经过时间 * 100%
            // 由于是进程的总CPU时间，需要除以CPU核心数得到单核使用率
            uint32_t cores = sysconf(_SC_NPROCESSORS_ONLN);
            info.cpuPercent = cpuTimeDiff * 100.0 / timeDiff;
            info.cpuPercentPerCore = info.cpuPercent / cores;
        }
        
        // 更新上次记录
        m_lastTotalTime = info.totalTime;
        m_lastUserTime = info.userTime;
        m_lastSystemTime = info.systemTime;
        m_lastTimestamp = now;
    }
    
    void readMemInfo(SystemResourceInfo& info) {
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo.is_open()) {
            return;
        }
        
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.compare(0, 9, "MemTotal:") == 0) {
                sscanf(line.c_str(), "MemTotal: %lu kB", &info.totalMem);
            } else if (line.compare(0, 8, "MemFree:") == 0) {
                sscanf(line.c_str(), "MemFree: %lu kB", &info.freeMem);
            } else if (line.compare(0, 13, "MemAvailable:") == 0) {
                sscanf(line.c_str(), "MemAvailable: %lu kB", &info.availableMem);
            } else if (line.compare(0, 9, "SwapTotal:") == 0) {
                sscanf(line.c_str(), "SwapTotal: %lu kB", &info.totalSwap);
            } else if (line.compare(0, 8, "SwapFree:") == 0) {
                unsigned long swapFree = 0;
                sscanf(line.c_str(), "SwapFree: %lu kB", &swapFree);
                info.usedSwap = info.totalSwap - swapFree;
            }
        }
        
        // 计算使用量
        info.usedMem = info.totalMem - info.availableMem;
        if (info.totalMem > 0) {
            info.memoryUsage = info.usedMem * 100.0 / info.totalMem;
        }
    }
    
    void readLoadAvg(SystemResourceInfo& info) {
        std::ifstream loadavg("/proc/loadavg");
        if (!loadavg.is_open()) {
            return;
        }
        
        loadavg >> info.loadAvg1 >> info.loadAvg5 >> info.loadAvg15;
    }
    
    void calculateSystemCpu(SystemResourceInfo& info) {
        std::ifstream statFile("/proc/stat");
        if (!statFile.is_open()) {
            return;
        }
        
        std::string line;
        std::getline(statFile, line);
        
        // 格式: cpu  user nice system idle iowait irq softirq steal guest guest_nice
        unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
        if (sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4) {
            return;
        }
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t idleTotal = idle + iowait;
        
        uint64_t now = getCurrentTimestamp();
        uint64_t timeDiff = now - m_lastSysTimestamp;
        
        if (timeDiff > 0 && m_lastSysTimestamp > 0) {
            uint64_t totalDiff = total - m_lastSysTotal;
            uint64_t idleDiff = idleTotal - m_lastSysIdle;
            
            if (totalDiff > 0) {
                info.cpuUsage = (totalDiff - idleDiff) * 100.0 / totalDiff;
            }
        }
        
        m_lastSysTotal = total;
        m_lastSysIdle = idleTotal;
        m_lastSysTimestamp = now;
    }
    
    uint64_t getCurrentTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000 + 
               static_cast<uint64_t>(ts.tv_nsec) / 1000000;
    }
};

} // namespace IPC

#endif // PROCESS_MONITOR_H
