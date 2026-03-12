#pragma once
#ifndef EASYLOGGINGHELPER_H
#define EASYLOGGINGHELPER_H
#include "easylogging++.h"
#include <dirent.h>
#include <thread>
#include <unistd.h>

INITIALIZE_EASYLOGGINGPP

namespace el 
{
    static int LogCleanDays = 30;  
    static std::string LogRootPath = "./logs/";
    static el::base::SubsecondPrecision LogSsPrec(3);
    static std::string LoggerToday = el::base::utils::DateTime::getDateTime("%Y%M%d", &LogSsPrec);

    //删除文件路径下n天前的日志文件，由于删除日志文件导致的空文件夹会在下一次删除
    //isRoot为true时，只会清理空的子文件夹
    void DeleteOldFiles(std::string path, int oldDays, bool isRoot)
    {
        // 基于当前系统的当前日期/时间
        time_t nowTime = time(nullptr);
        struct tm *localTime = localtime(&nowTime);
        nowTime = mktime(localTime); // 转换为当天0点的时间戳，以便精确计算天数

        DIR *dir = opendir(path.c_str());
        if (dir == nullptr) {
            std::cerr << "Error opening directory: " << path << std::endl;
            return;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string fileName = ent->d_name;
            std::string fullPath = path + "/" + fileName;

            // 忽略"."和".."目录
            if (fileName == "." || fileName == "..") continue;

            struct stat st;
            if (stat(fullPath.c_str(), &st) == -1) {
                perror("stat");
                continue;
            }

            // 如果是目录，递归处理
            if (S_ISDIR(st.st_mode)) {
                DeleteOldFiles(fullPath, oldDays, false);
            }
            // 如果是文件且是.log文件
            else if (S_ISREG(st.st_mode) && fileName.size() > 4 && fileName.substr(fileName.size() - 4) == ".log") {
                // 检查文件修改时间是否超过oldDays天
                time_t modTime = st.st_mtime;
                double diffDays = difftime(nowTime, modTime) / (60 * 60 * 24); // 秒转换为天
                if (diffDays > oldDays) {
                    // 删除文件
                    if (unlink(fullPath.c_str()) == -1) {
                        perror("unlink");
                    }
                }
            }
        }
        closedir(dir);

        if (!isRoot) {
            // 检查并删除空目录
            if (readdir(dir) == nullptr) { // 重新打开并检查是否为空
                dir = opendir(path.c_str());
                if (dir != nullptr && readdir(dir) == nullptr) {
                    closedir(dir);
                    rmdir(path.c_str()); // 删除空目录
                } else if (dir != nullptr) {
                    closedir(dir);
                }
            }
        }
    }
    
    static void ConfigureLogger()
    {       
        std::string datetimeY = el::base::utils::DateTime::getDateTime("%Y", &LogSsPrec);
        std::string datetimeYM = el::base::utils::DateTime::getDateTime("%Y%M", &LogSsPrec);
        std::string datetimeYMd = el::base::utils::DateTime::getDateTime("%Y%M%d", &LogSsPrec);
        
        std::string filePath = LogRootPath + "/" + datetimeYM + "/" + datetimeYMd + "/";
        std::string filename;

        el::Configurations defaultConf;
        defaultConf.setToDefault();
        //建议使用setGlobally
        defaultConf.setGlobally(el::ConfigurationType::Format, "%datetime %msg");
        defaultConf.setGlobally(el::ConfigurationType::Enabled, "true");
        defaultConf.setGlobally(el::ConfigurationType::ToFile, "true");
        defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "true");
        defaultConf.setGlobally(el::ConfigurationType::SubsecondPrecision, "6");
        defaultConf.setGlobally(el::ConfigurationType::PerformanceTracking, "true");
        defaultConf.setGlobally(el::ConfigurationType::LogFlushThreshold, "1");

        //限制文件大小时配置
        //defaultConf.setGlobally(el::ConfigurationType::MaxLogFileSize, "2097152");

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Global)+".log";
        defaultConf.set(el::Level::Global, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Debug) + ".log";
        defaultConf.set(el::Level::Debug, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Error) + ".log";
        defaultConf.set(el::Level::Error, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Fatal) + ".log";
        defaultConf.set(el::Level::Fatal, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Info) + ".log";
        defaultConf.set(el::Level::Info, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Trace) + ".log";
        defaultConf.set(el::Level::Trace, el::ConfigurationType::Filename, filePath + filename);

        filename = datetimeYMd + "_" + el::LevelHelper::convertToString(el::Level::Warning) + ".log";
        defaultConf.set(el::Level::Warning, el::ConfigurationType::Filename, filePath + filename);        

        el::Loggers::reconfigureLogger("default", defaultConf);

        //限制文件大小时启用
        //el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
    }

    class LogDispatcher : public el::LogDispatchCallback
    {
    protected:
        void handle(const el::LogDispatchData* data) noexcept override {
            m_data = data;
            // 使用记录器的默认日志生成器进行调度
            dispatch(m_data->logMessage()->logger()->logBuilder()->build(m_data->logMessage(),
                m_data->dispatchAction() == el::base::DispatchAction::NormalLog));

            //此处也可以写入数据库
        }
    private:
        const el::LogDispatchData* m_data;
        void dispatch(el::base::type::string_t&& logLine) noexcept
        {
            el::base::SubsecondPrecision ssPrec(3);
            static std::string now = el::base::utils::DateTime::getDateTime("%Y%M%d", &ssPrec);
            if (now != LoggerToday)
            {
                LoggerToday = now;
                ConfigureLogger();
                std::thread task(el::DeleteOldFiles, LogRootPath, LogCleanDays, true);
            }
        }
    };

    static void InitEasylogging()
    {
        ConfigureLogger();

        el::Helpers::installLogDispatchCallback<LogDispatcher>("LogDispatcher");
        LogDispatcher* dispatcher = el::Helpers::logDispatchCallback<LogDispatcher>("LogDispatcher");
        dispatcher->setEnabled(true);
    }
}
#endif 
