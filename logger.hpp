#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <sstream>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <format>
#include <filesystem>
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
    Logger(LogLevel level = LogLevel::INFO) : level_(level) {}

    ~Logger() = default; // std::unique_ptr will handle the file stream automatically

    void set_level(LogLevel level)
    {
        level_ = level;
    }

    void set_level(const std::string &level_str)
    {
        static const std::unordered_map<std::string, LogLevel> level_map = {
            {"TRACE", LogLevel::TRACE},
            {"DEBUG", LogLevel::DEBUG},
            {"INFO", LogLevel::INFO},
            {"STEP", LogLevel::STEP},
            {"WARNING", LogLevel::WARNING},
            {"ERROR", LogLevel::ERROR},
            {"FATAL", LogLevel::FATAL}};

        std::string upper_level = level_str;
        std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(), ::toupper);
        auto it = level_map.find(upper_level);
        if (it != level_map.end())
        {
            level_ = it->second;
        }
        else
        {
            level_ = LogLevel::INFO;
        }
    }

    void set_logfile(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_stream_)
        {
            file_stream_->close();
            file_stream_.reset();
        }
        if (!path.empty())
        {
            file_stream_ = std::make_unique<std::ofstream>(path, std::ios::app);
        }
    }

    template <typename... Args>
    void log_impl(LogLevel msg_level, std::string_view file, int line, const std::string &fmt_str, Args &&...args)
    {
        if (msg_level < level_)
            return;

        auto now = std::chrono::system_clock::now();

        std::string msg = std::vformat(fmt_str, std::make_format_args(args...));

        std::lock_guard<std::mutex> lock(mutex_);

        // Format and print header to both streams
        auto print_header = [&](std::ostream &os)
        {
            const auto zoned_time = std::chrono::zoned_time{std::chrono::current_zone(), now};
            // Floor the time to seconds to prevent std::format from adding its own fractional part.
            const auto seconds_part = std::chrono::floor<std::chrono::seconds>(zoned_time.get_local_time());

            os << std::format("{:%Y-%m-%d_%H:%M:%S}", seconds_part) << "."
               << std::format("{:03}", std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000)
               << "-[" << level_to_string(msg_level) << "] ";
            if (line > 0)
            {
                os << std::filesystem::path(file).filename().string() << ":" << line << " ";
            }
        };

        print_header(std::cout);
        std::cout << msg << std::endl;

        if (file_stream_ && file_stream_->is_open())
        {
            print_header(*file_stream_);
            *file_stream_ << msg << std::endl;
        }
    }

private:
    LogLevel level_;
    std::mutex mutex_;
    std::unique_ptr<std::ofstream> file_stream_;

    static const char *level_to_string(LogLevel level)
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
            return "UNKWN";
        }
    }
};

// Use macros to automatically capture file and line number
#define LOG_TRACE(logger, fmt, ...) (logger).log_impl(LogLevel::TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(logger, fmt, ...) (logger).log_impl(LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(logger, fmt, ...) (logger).log_impl(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_STEP(logger, fmt, ...) (logger).log_impl(LogLevel::STEP, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARNING(logger, fmt, ...) (logger).log_impl(LogLevel::WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(logger, fmt, ...) (logger).log_impl(LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(logger, fmt, ...) (logger).log_impl(LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
