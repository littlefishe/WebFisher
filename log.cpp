#include "log.h"
#include <map>
#include <iostream>
#include <functional>
#include <time.h>
#include <string.h>
#include "format.h"

namespace fisher {

const char* LogLevel::ToString(LogLevel::Level level) {
    switch(level) {
#define XX(name) \
    case LogLevel::name: \
        return #name; \
        break;

    XX(DEBUG);
    XX(INFO);
    XX(WARN);
    XX(ERROR);
    XX(FATAL);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

LogLevel::Level LogLevel::FromString(const std::string& str) {
#define XX(level, v) \
    if(str == #v) { \
        return LogLevel::level; \
    }
    XX(DEBUG, debug);
    XX(INFO, info);
    XX(WARN, warn);
    XX(ERROR, error);
    XX(FATAL, fatal);

    XX(DEBUG, DEBUG);
    XX(INFO, INFO);
    XX(WARN, WARN);
    XX(ERROR, ERROR);
    XX(FATAL, FATAL);
    return LogLevel::UNKNOW;
#undef XX
}

LogEventWrap::LogEventWrap(LogEvent::LogEventRef e)
    :event_(e) {
}

LogEventWrap::~LogEventWrap() {
    event_->getLogger()->log(event_->getLevel(), event_);
}

void LogEvent::format(const char* fmt, ...) {
    va_list al;
    va_start(al, fmt);
    format(fmt, al);
    va_end(al);
}

void LogEvent::format(const char* fmt, va_list al) {
    char* buf = nullptr;
    int len = vasprintf(&buf, fmt, al);
    if(len != -1) {
        ss_ << std::string(buf, len);
        free(buf);
    }
}

std::stringstream& LogEventWrap::getSS() {
    return event_->getSS();
}


void LogAppender::setFormatter(LogFormatter::LogFormatterRef val) {
    std::unique_lock ul(latch_);
    formatter_ = val;
    if(formatter_) {
        hasFormatter_ = true;
    } else {
        hasFormatter_ = false;
    }
}

LogFormatter::LogFormatterRef LogAppender::getFormatter() {
    std::unique_lock ul(latch_);
    return formatter_;
}


LogEvent::LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level
            ,const char* file, int32_t line, uint32_t elapse
            ,uint32_t fiber_id, uint64_t time
            ,uint64_t thread_id, const std::string& thread_name)
    :file_(file)
    ,line_(line)
    ,elapse_(elapse)
    ,time_(time)
    ,tid_(thread_id)
    ,tname_(thread_name)
    ,fid_(fiber_id)
    ,logger_(logger)
    ,level_(level) {
}

Logger::Logger(const std::string& name)
    :name_(name), level_(LogLevel::INFO) {
    formatter_.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

void Logger::setFormatter(LogFormatter::LogFormatterRef val) {
    std::unique_lock ul(latch_);
    formatter_ = val;

    for(auto& ap : appenders_) {
        std::unique_lock sul(ap->latch_);
        if(!ap->hasFormatter_) {
            ap->setFormatter(val);
        }
    }
}

void Logger::setFormatter(const std::string& val) {
    LogFormatter::LogFormatterRef new_val(new LogFormatter(val));
    if(new_val->isError()) {
        std::cout << "Logger setFormatter name=" << name_
                  << " value=" << val << " invalid formatter"
                  << std::endl;
        return;
    }
    setFormatter(new_val);
}

std::string Logger::toYamlString() {
    // std::unique_lock ul(latch_);
    // YAML::Node node;
    // node["name"] = name_;
    // if(level_ != LogLevel::UNKNOW) {
    //     node["level"] = LogLevel::ToString(level_);
    // }
    // if(formatter_) {
    //     node["formatter"] = formatter_->getPattern();
    // }

    // for(auto& i : appenders_) {
    //     node["appenders"].push_back(YAML::Load(i->toYamlString()));
    // }
    // std::stringstream ss;
    // ss << node;
    // return ss.str();
}


LogFormatter::LogFormatterRef Logger::getFormatter() {
    std::unique_lock ul(latch_);
    return formatter_;
}

void Logger::addAppender(LogAppender::LogAppenderRef appender) {
    std::unique_lock ul(latch_);
    if(!appender->getFormatter()) {
        std::unique_lock ul(appender->latch_);
        appender->formatter_ = formatter_;
    }
    appenders_.push_back(appender);
}

void Logger::delAppender(LogAppender::LogAppenderRef appender) {
    std::unique_lock ul(latch_);
    appenders_.erase(std::find(appenders_.begin(), appenders_.end(), appender)); 
}

void Logger::clearAppenders() {
    std::unique_lock ul(latch_);
    appenders_.clear();
}

void Logger::log(LogLevel::Level level, LogEvent::LogEventRef event) {
    if(level >= level_) {
        auto self = shared_from_this();
        std::unique_lock ul(latch_);
        for(auto& ap : appenders_) {
            ap->log(self, level, event);
        }
    }
}


FileLogAppender::FileLogAppender(const std::string& filename)
    :filename_(filename) {
    reopen();
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::LogEventRef event) {
    if(level >= level_) {
        std::unique_lock ul(latch_);
        formatter_->format(filestream_, event);
    }
}

std::string FileLogAppender::toYamlString() {
    // std::unique_lock ul(latch_);
    // YAML::Node node;
    // node["type"] = "FileLogAppender";
    // node["file"] = filename_;
    // if(level_ != LogLevel::UNKNOW) {
    //     node["level"] = LogLevel::ToString(level_);
    // }
    // if(hasFormatter_ && formatter_) {
    //     node["formatter"] = formatter_->getPattern();
    // }
    // std::stringstream ss;
    // ss << node;
    // return ss.str();
}

bool FileLogAppender::reopen() {
    std::unique_lock ul(latch_);
    if(filestream_) {
        filestream_.close();
    }
    filestream_.open(filename_.c_str(), std::ios::app);
    return filestream_.is_open();
}

void StdoutLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::LogEventRef event) {
    if(level >= level_) {
        std::unique_lock ul(latch_);
        formatter_->format(std::cout, event);
    }
}

std::string StdoutLogAppender::toYamlString() {
    // std::unique_lock ul(latch_);
    // YAML::Node node;
    // node["type"] = "StdoutLogAppender";
    // if(level_ != LogLevel::UNKNOW) {
    //     node["level"] = LogLevel::ToString(level_);
    // }
    // if(hasFormatter_ && formatter_) {
    //     node["formatter"] = formatter_->getPattern();
    // }
    // std::stringstream ss;
    // ss << node;
    // return ss.str();
}

LogFormatter::LogFormatter(const std::string& pattern)
    :pattern_(pattern) {
    init();
}

std::string LogFormatter::format(LogEvent::LogEventRef event) {
    std::stringstream ss;
    for(auto& i : items_) {
        i->format(ss, event);
    }
    return ss.str();
}

std::ostream& LogFormatter::format(std::ostream& ofs, LogEvent::LogEventRef event) {
    for(auto& i : items_) {
        i->format(ofs, event);
    }
    return ofs;
}

//%xxx %xxx{xxx} %%
void LogFormatter::init() {
    //str, format, type
    std::vector<std::tuple<std::string, std::string, int> > vec;
    format_parser(vec, pattern_); 

    static std::map<std::string, std::function<FormatItem::FormatItemRef(const std::string& str)> > s_format_items = {
#define XX(str, C) \
        {#str, [](const std::string& fmt) { return FormatItem::FormatItemRef(new C(fmt));}}

        XX(m, MessageFormatItem),           //m:消息
        XX(p, LevelFormatItem),             //p:日志级别
        XX(r, ElapseFormatItem),            //r:累计毫秒数
        XX(c, NameFormatItem),              //c:日志名称
        XX(t, ThreadIdFormatItem),          //t:线程id
        XX(n, NewLineFormatItem),           //n:换行
        XX(d, DateTimeFormatItem),          //d:时间
        XX(f, FilenameFormatItem),          //f:文件名
        XX(l, LineFormatItem),              //l:行号
        XX(T, TabFormatItem),               //T:Tab
        XX(F, FiberIdFormatItem),           //F:协程id
        XX(N, ThreadNameFormatItem),        //N:线程名称
#undef XX
    };

    for(auto& i : vec) {
        if(std::get<2>(i) == 0) {
            items_.push_back(FormatItem::FormatItemRef(new StringFormatItem(std::get<0>(i))));
        } else {
            auto it = s_format_items.find(std::get<0>(i));
            if(it == s_format_items.end()) {
                items_.push_back(FormatItem::FormatItemRef(new StringFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
                error_ = true;
            } else {
                items_.push_back(it->second(std::get<1>(i)));
            }
        }
    }
}


LoggerManager::LoggerManager() {
    root_.reset(new Logger);
    root_->addAppender(LogAppender::LogAppenderRef(new StdoutLogAppender));

    loggers_[root_->name_] = root_;

    init();
}

Logger::LoggerRef LoggerManager::getLogger(const std::string& name) {
    std::unique_lock ul(latch_);
    auto it = loggers_.find(name);
    if(it != loggers_.end()) {
        return it->second;
    }

    Logger::LoggerRef logger(new Logger(name));
    logger->addAppender(LogAppender::LogAppenderRef(new StdoutLogAppender));
    loggers_[name] = logger;
    return loggers_[name];
}

struct LogAppenderDefine {
    int type = 0; //1 File, 2 Stdout
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::string file;

    bool operator==(const LogAppenderDefine& oth) const {
        return type == oth.type
            && level == oth.level
            && formatter == oth.formatter
            && file == oth.file;
    }
};



std::string LoggerManager::toYamlString() {
    // std::unique_lock ul(latch_);
    // YAML::Node node;
    // for(auto& i : loggers_) {
    //     node.push_back(YAML::Load(i.second->toYamlString()));
    // }
    // std::stringstream ss;
    // ss << node;
    // return ss.str();
}

void LoggerManager::init() {
}

}