#include <format>
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <tuple>
#include <utility>

enum class LogLevel
{
    TRACE,
    DEBUG,
    STEP,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger
{
public:
    Logger(LogLevel level = LogLevel::INFO) : level_(level), file_stream_(nullptr) {}
    ~Logger()
    {
        if (file_stream_)
        {
            file_stream_->close();
            delete file_stream_;
        }
    }

    void set_level(LogLevel level)
    {
        level_ = level;
    }

    void set_level(const std::string &level_str)
    {
        std::string lvl = level_str;
        std::transform(lvl.begin(), lvl.end(), lvl.begin(), ::toupper);
        if (lvl == "TRACE")
            level_ = LogLevel::TRACE;
        else if (lvl == "DEBUG")
            level_ = LogLevel::DEBUG;
        else if (lvl == "INFO")
            level_ = LogLevel::INFO;
        else if (lvl == "STEP")
            level_ = LogLevel::STEP;
        else if (lvl == "WARNING")
            level_ = LogLevel::WARNING;
        else if (lvl == "ERROR")
            level_ = LogLevel::ERROR;
        else if (lvl == "FATAL")
            level_ = LogLevel::FATAL;
        else
            level_ = LogLevel::INFO;
    }

    void set_logfile(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_stream_)
        {
            file_stream_->close();
            delete file_stream_;
            file_stream_ = nullptr;
        }
        if (!path.empty())
        {
            file_stream_ = new std::ofstream(path, std::ios::app);
        }
    }

    template <typename... Args>
    void log(LogLevel msg_level, const std::string &fmt_str, Args &&...args)
    {
        log_impl(msg_level, fmt_str, "", 0, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void log_impl(LogLevel msg_level, const std::string &fmt_str, const char *file, int line, Args &&...args)
    {
        if (msg_level < level_)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto duration = now.time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
        micros = static_cast<int>(micros / 1000);

        std::ostringstream time_ss;
        time_ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H:%M:%S");
        time_ss << "." << std::setfill('0') << std::setw(3) << micros;

        std::string level_str = level_to_string(msg_level);
        std::tuple<std::decay_t<Args>...> arg_tuple(std::forward<Args>(args)...);
        std::string msg = std::apply(
            [&](auto &...a)
            { return std::vformat(fmt_str, std::make_format_args(a...)); },
            arg_tuple);

        std::ostringstream out;
        if (file && line > 0)
            out << time_ss.str() << "-[" << level_str << "] " << file << ":" << line << " " << msg << std::endl;
        else
            out << time_ss.str() << "-[" << level_str << "] " << msg << std::endl;

        if (file_stream_ && file_stream_->is_open())
            *file_stream_ << out.str();
        std::cout << out.str();
    }

#define LOG_TRACE(logger, fmt, ...) (logger).log_impl(LogLevel::TRACE, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(logger, fmt, ...) (logger).log_impl(LogLevel::DEBUG, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(logger, fmt, ...) (logger).log_impl(LogLevel::INFO, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_STEP(logger, fmt, ...) (logger).log_impl(LogLevel::STEP, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARNING(logger, fmt, ...) (logger).log_impl(LogLevel::WARNING, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(logger, fmt, ...) (logger).log_impl(LogLevel::ERROR, fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_FATAL(logger, fmt, ...) (logger).log_impl(LogLevel::FATAL, fmt, __FILE__, __LINE__, ##__VA_ARGS__)

    template <typename... Args>
    void trace(const std::string &fmt_str, Args &&...args) { log(LogLevel::TRACE, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void debug(const std::string &fmt_str, Args &&...args) { log(LogLevel::DEBUG, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void info(const std::string &fmt_str, Args &&...args) { log(LogLevel::INFO, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void step(const std::string &fmt_str, Args &&...args) { log(LogLevel::STEP, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void warning(const std::string &fmt_str, Args &&...args) { log(LogLevel::WARNING, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void error(const std::string &fmt_str, Args &&...args) { log(LogLevel::ERROR, fmt_str, std::forward<Args>(args)...); }
    template <typename... Args>
    void fatal(const std::string &fmt_str, Args &&...args) { log(LogLevel::FATAL, fmt_str, std::forward<Args>(args)...); }

private:
    LogLevel level_;
    std::mutex mutex_;
    std::ofstream *file_stream_ = nullptr;

    static std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO ";
        case LogLevel::STEP:
            return "STEP ";
        case LogLevel::WARNING:
            return "WARN ";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }
};