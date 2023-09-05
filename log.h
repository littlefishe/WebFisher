#pragma once

#include <string>
#include <iostream>
#include <memory>
#include <list>
#include <stdarg.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdarg.h>
#include <map>
#include <thread>
#include <mutex>
#include <unordered_set>
#include "singleton.h"
#include "util.h"

/**
 * @brief 使用流式方式将日志级别level的日志写入到logger
 */
#define FISHER_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        fisher::LogEventWrap(fisher::LogEvent::LogEventRef(new fisher::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, fisher::GetFiberId(), time(0), fisher::GetThreadId(), fisher::GetThreadName()))).getSS()

/**
 * @brief 使用流式方式将日志级别debug的日志写入到logger
 */
#define FISHER_LOG_DEBUG(logger) FISHER_LOG_LEVEL(logger, fisher::LogLevel::DEBUG)

/**
 * @brief 使用流式方式将日志级别info的日志写入到logger
 */
#define FISHER_LOG_INFO(logger) FISHER_LOG_LEVEL(logger, fisher::LogLevel::INFO)

/**
 * @brief 使用流式方式将日志级别warn的日志写入到logger
 */
#define FISHER_LOG_WARN(logger) FISHER_LOG_LEVEL(logger, fisher::LogLevel::WARN)

/**
 * @brief 使用流式方式将日志级别error的日志写入到logger
 */
#define FISHER_LOG_ERROR(logger) FISHER_LOG_LEVEL(logger, fisher::LogLevel::ERROR)

/**
 * @brief 使用流式方式将日志级别fatal的日志写入到logger
 */
#define FISHER_LOG_FATAL(logger) FISHER_LOG_LEVEL(logger, fisher::LogLevel::FATAL)

/**
 * @brief 获取主日志器
 */
#define FISHER_LOG_ROOT() fisher::SglLogMgr::getInstance().getRoot()

/**
 * @brief 获取name的日志器
 */
#define FISHER_LOG_NAME(name) fisher::SglLogMgr::getInstance().getLogger(name)

namespace fisher {

class Logger;
class LoggerManager;

/**
 * @brief 日志级别
 */
class LogLevel {
public:
    /**
     * @brief 日志级别枚举
     */
    enum Level {
        /// 未知级别
        UNKNOW = 0,
        /// INFO 级别
        INFO = 2,
        /// DEBUG 级别
        DEBUG = 1,
        /// WARN 级别
        WARN = 3,
        /// ERROR 级别
        ERROR = 4,
        /// FATAL 级别
        FATAL = 5
    };

    /**
     * @brief 将日志级别转成文本输出
     * @param[in] level 日志级别
     */
    static const char* ToString(LogLevel::Level level);
    
    /**
     * @brief 将文本转换成日志级别
     * @param[in] str 日志级别文本
     */
    static LogLevel::Level FromString(const std::string& str);
};

/**
 * @brief 日志事件
 */
class LogEvent {
public:
    using LogEventRef = std::shared_ptr<LogEvent>;
    /**
     * @brief 构造函数
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] file 文件名
     * @param[in] line 文件行号
     * @param[in] elapse 程序启动依赖的耗时(毫秒)
     * @param[in] thread_id 线程id
     * @param[in] fiber_id 协程id
     * @param[in] time 日志事件(秒)
     * @param[in] thread_name 线程名称
     */
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level
            ,const char* file, int32_t line, uint32_t elapse
            ,uint32_t fiber_id, uint64_t time
            ,uint64_t thread_id, const std::string& thread_name);

    /**
     * @brief 返回文件名
     */
    const char* getFile() const { return file_;}

    /**
     * @brief 返回行号
     */
    int32_t getLine() const { return line_;}

    /**
     * @brief 返回耗时
     */
    uint32_t getElapse() const { return elapse_;}

    /**
     * @brief 返回线程ID
     */
    uint64_t getThreadId() const { return tid_;}

    /**
     * @brief 返回协程ID
     */
    uint32_t getFiberId() const { return fid_;}

    /**
     * @brief 返回时间
     */
    uint64_t getTime() const { return time_;}

    /**
     * @brief 返回线程名称
     */
    const std::string& getThreadName() const { return tname_;}

    /**
     * @brief 返回日志内容
     */
    std::string getContent() const { return ss_.str();}

    /**
     * @brief 返回日志器
     */
    std::shared_ptr<Logger> getLogger() const { return logger_;}

    /**
     * @brief 返回日志级别
     */
    LogLevel::Level getLevel() const { return level_;}

    /**
     * @brief 返回日志内容字符串流
     */
    std::stringstream& getSS() { return ss_;}

    /**
     * @brief 格式化写入日志内容
     */
    void format(const char* fmt, ...);

    /**
     * @brief 格式化写入日志内容
     */
    void format(const char* fmt, va_list al);

private:
    const char* file_ = nullptr;
    int32_t line_ = 0;
    uint32_t elapse_ = 0;
    uint64_t time_ = 0;

    std::uint64_t tid_;
    std::string tname_;
    uint32_t fid_;

    std::stringstream ss_;
    std::shared_ptr<Logger> logger_;
    LogLevel::Level level_;
};


/**
 * @brief 日志事件包装器
 */
class LogEventWrap {
public:

    /**
     * @brief 构造函数
     * @param[in] e 日志事件
     */
    LogEventWrap(LogEvent::LogEventRef e);

    /**
     * @brief 析构函数
     */
    ~LogEventWrap();

    /**
     * @brief 获取日志事件
     */
    LogEvent::LogEventRef getEvent() const { return event_;}

    /**
     * @brief 获取日志内容流
     */
    std::stringstream& getSS();
private:
    /**
     * @brief 日志事件
     */
    LogEvent::LogEventRef event_;
};


/**
 * @brief 日志格式化
 */
class LogFormatter {
public:
    using LogFormatterRef = std::shared_ptr<LogFormatter>;
    /**
     * @brief 构造函数
     * @param[in] pattern 格式模板
     * @details 
     *  %m 消息
     *  %p 日志级别
     *  %r 累计毫秒数
     *  %c 日志名称
     *  %t 线程id
     *  %n 换行
     *  %d 时间
     *  %f 文件名
     *  %l 行号
     *  %T 制表符
     *  %F 协程id
     *  %N 线程名称
     *
     *  默认格式 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
     */
    LogFormatter(const std::string& pattern);

    /**
     * @brief 返回格式化日志文本
     * @param[in] event 日志事件
     */
    std::string format(LogEvent::LogEventRef event);
    std::ostream& format(std::ostream& ofs, LogEvent::LogEventRef event);


    /**
     * @brief 日志内容项格式化
     */
    class FormatItem {
    public:
        using FormatItemRef = std::shared_ptr<FormatItem>;
        /**
         * @brief 析构函数
         */
        virtual ~FormatItem() {}
        /**
         * @brief 格式化日志到流
         * @param[in, out] os 日志输出流
         * @param[in] logger 日志器
         * @param[in] level 日志等级
         * @param[in] event 日志事件
         */
        virtual void format(std::ostream& os, LogEvent::LogEventRef event) = 0;
    };

    /**
     * @brief 初始化,解析日志模板
     */
    void init();

    /**
     * @brief 是否有错误
     */
    bool isError() const { return error_;}

    /**
     * @brief 返回日志模板
     */
    const std::string getPattern() const { return pattern_;}

private:
    std::string pattern_;
    std::vector<FormatItem::FormatItemRef> items_;
    bool error_ = false;

};

/**
 * @brief 日志输出目标
 */
class LogAppender {
friend class Logger;
public:
    using LogAppenderRef = std::shared_ptr<LogAppender>;

    /**
     * @brief 析构函数
     */
    virtual ~LogAppender() {}

    /**
     * @brief 写入日志
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::LogEventRef event) = 0;

    /**
     * @brief 将日志输出目标的配置转成YAML String
     */
    virtual std::string toYamlString() = 0;

    /**
     * @brief 更改日志格式器
     */
    void setFormatter(LogFormatter::LogFormatterRef val);

    /**
     * @brief 获取日志格式器
     */
    LogFormatter::LogFormatterRef getFormatter();

    /**
     * @brief 获取日志级别
     */
    LogLevel::Level getLevel() const { return level_;}

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel::Level val) { level_ = val;}

protected:
    LogLevel::Level level_ = LogLevel::DEBUG;
    bool hasFormatter_ = false;
    std::mutex latch_;
    LogFormatter::LogFormatterRef formatter_;
};

/**
 * @brief 日志器
 */
class Logger : public std::enable_shared_from_this<Logger> {
friend class LoggerManager;
public:
    using LoggerRef = std::shared_ptr<Logger>;

    /**
     * @brief 构造函数
     * @param[in] name 日志器名称
     */
    Logger(const std::string& name = "root");

    /**
     * @brief 写日志
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    void log(LogLevel::Level level, LogEvent::LogEventRef event);

    /**
     * @brief 添加日志目标
     * @param[in] appender 日志目标
     */
    void addAppender(LogAppender::LogAppenderRef appender);

    /**
     * @brief 删除日志目标
     * @param[in] appender 日志目标
     */
    void delAppender(LogAppender::LogAppenderRef appender);

    /**
     * @brief 清空日志目标
     */
    void clearAppenders();

    /**
     * @brief 返回日志级别
     */
    LogLevel::Level getLevel() const { return level_;}

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel::Level val) { level_ = val;}

    /**
     * @brief 返回日志名称
     */
    const std::string& getName() const { return name_;}

    /**
     * @brief 设置日志格式器
     */
    void setFormatter(LogFormatter::LogFormatterRef val);

    /**
     * @brief 设置日志格式模板
     */
    void setFormatter(const std::string& val);

    /**
     * @brief 获取日志格式器
     */
    LogFormatter::LogFormatterRef getFormatter();

    /**
     * @brief 将日志器的配置转成YAML String
     */
    std::string toYamlString();

private:
    std::string name_;
    LogLevel::Level level_;
    std::mutex latch_;
    std::list<LogAppender::LogAppenderRef> appenders_;
    LogFormatter::LogFormatterRef formatter_;
};

/**
 * @brief 输出到控制台的Appender
 */
class StdoutLogAppender : public LogAppender {
public:
    using StdoutLogAppenderRef = std::shared_ptr<StdoutLogAppender>;
    
    void log(Logger::LoggerRef logger, LogLevel::Level level, LogEvent::LogEventRef event) override;
    std::string toYamlString() override;
};

/**
 * @brief 输出到文件的Appender
 */
class FileLogAppender : public LogAppender {
public:
    using FileLogAppenderRef = std::shared_ptr<FileLogAppender>;
    
    FileLogAppender(const std::string& filename);
    void log(Logger::LoggerRef logger, LogLevel::Level level, LogEvent::LogEventRef event) override;
    std::string toYamlString() override;
    bool reopen();

private:
    std::string filename_;
    std::ofstream filestream_;
    uint64_t lastTime_ = 0;
};

/**
 * @brief 日志器管理类
 */
class LoggerManager {
public:
    /**
     * @brief 构造函数
     */
    LoggerManager();

    /**
     * @brief 获取日志器
     * @param[in] name 日志器名称
     */
    Logger::LoggerRef getLogger(const std::string& name);

    /**
     * @brief 初始化
     */
    void init();

    /**
     * @brief 返回主日志器
     */
    Logger::LoggerRef getRoot() const { return root_;}

    /**
     * @brief 将所有的日志器配置转成YAML String
     */
    std::string toYamlString();
    
private:
    std::mutex latch_;
    std::map<std::string, Logger::LoggerRef> loggers_;
    Logger::LoggerRef root_;
};

// singleton
using SglLogMgr = Singleton<LoggerManager>;

}
