#ifndef PLUGIN_H
#define PLUGIN_H

#include <QObject>
#include <ctime>
#include "../CoreLib_global.h"

// 插件接口基类
// 注意：使用 const char* 而非 std::string 避免跨动态库 ABI 问题
class CORELIB_EXPORT Plugin : public QObject {
    Q_OBJECT
public:
    Plugin() : m_startTime(0) {}
    virtual ~Plugin() {}

    virtual void start() = 0;
    virtual void end() = 0;
    virtual const char* getVersion() = 0;
    virtual const char* getBuildTimeStr() = 0;

    // 可选的虚函数，子类可以实现
    virtual const char* getName() const { return ""; }
    virtual const char* getDescription() const { return ""; }

    // 获取运行时间（秒）
    virtual size_t getUptimeSeconds() const {
        if (m_startTime == 0) return 0;
        return static_cast<size_t>(time(nullptr) - m_startTime);
    }

protected:
    time_t m_startTime;  // 启动时间
};

#endif // PLUGIN_H
