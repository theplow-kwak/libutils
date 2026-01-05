#pragma once

#include <iostream>
#include <string>
#include <string_view>
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
#include <type_traits>
#include <cctype>

#define LIBUTILS_HAS_FMT 0

enum class LogLevel
{
    TRACE,
    DEBUG,
    STEP,
    INFO,
    WARNING,
    L_ERROR,
    FATAL
};

class Logger
{
public:
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    static Logger &get()
    {
        static Logger instance;
        return instance;
    }

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
            {"ERROR", LogLevel::L_ERROR},
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

        std::string msg;
        try
        {
            // First, try to format using std::vformat, assuming std::format style.
            if constexpr (sizeof...(args) > 0)
            {
                msg = std::vformat(fmt_str, std::make_format_args(args...));
            }
            else
            {
                msg = fmt_str;
            }
        }
        catch (const std::format_error &)
        {
            // If std::vformat fails, fall back to printf-style formatting.
            if constexpr (sizeof...(args) > 0)
            {
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                msg = format_printf(fmt_str, tup);
            }
            else
            {
                msg = fmt_str;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Format and print header to both streams
        auto print_header = [&](std::ostream &os)
        {
            // C++20 formatting for timestamp with milliseconds
            const auto time_in_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
            const auto zoned_time = std::chrono::zoned_time(std::chrono::current_zone(), time_in_ms);

            os << std::format("{:%Y-%m-%d_%H:%M:%S}", zoned_time)
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
    Logger() : level_(LogLevel::INFO) {}
    ~Logger() = default;

    LogLevel level_;
    std::mutex mutex_;
    std::unique_ptr<std::ofstream> file_stream_;

    template <typename Tuple>
    static std::string format_printf(std::string_view fmt, Tuple &tup)
    {
        std::ostringstream os;
        size_t arg_index = 0;
        size_t i = 0;

        while (i < fmt.size())
        {
            size_t percent_pos = fmt.find('%', i);
            if (percent_pos == std::string_view::npos)
            {
                os << fmt.substr(i);
                break;
            }

            os << fmt.substr(i, percent_pos - i);

            if (percent_pos + 1 >= fmt.size())
            {
                os << '%'; // Dangling '%' at the end of the string
                break;
            }

            if (fmt[percent_pos + 1] == '%')
            {
                os << '%';
                i = percent_pos + 2;
                continue;
            }

            size_t spec_end = fmt.find_first_of("diuoxXfFeEgGaAcspn", percent_pos + 1);

            if (spec_end == std::string_view::npos)
            {
                // Invalid format specifier, print literally
                os << fmt.substr(percent_pos, 1);
                i = percent_pos + 1;
                continue;
            }

            std::string_view spec_group = fmt.substr(percent_pos, spec_end - percent_pos + 1);
            os << format_printf_arg(tup, arg_index, spec_group);

            arg_index++;
            i = spec_end + 1;
        }
        return os.str();
    }

    template <typename T>
    static std::string format_one_arg(const T &v, std::string_view spec_group)
    {
        char buffer[1024];
        std::string fmt(spec_group);

        // Use snprintf for safe formatting.
        // This relies on default argument promotions for arithmetic types.
        if constexpr (std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>)
        {
            snprintf(buffer, sizeof(buffer), fmt.c_str(), std::string(v).c_str());
        }
        else if constexpr (std::is_convertible_v<std::decay_t<T>, const char *>)
        {
            snprintf(buffer, sizeof(buffer), fmt.c_str(), v);
        }
        else if constexpr (std::is_pointer_v<T>)
        {
            // For any other pointer, always use %p for safety, ignoring user's specifier.
            snprintf(buffer, sizeof(buffer), "%p", static_cast<const void *>(v));
        }
        else if constexpr (std::is_arithmetic_v<T>) // Catches integers, floats, bool
        {
            snprintf(buffer, sizeof(buffer), fmt.c_str(), v);
        }
        else
        {
            // Fallback for unformattable types
            return "[unformattable type]";
        }
        return buffer;
    }

    template <std::size_t I = 0, typename Tuple>
    static std::string format_printf_arg(Tuple &tup, size_t index, std::string_view spec_group)
    {
        if constexpr (I < std::tuple_size_v<std::remove_reference_t<Tuple>>)
        {
            if (I == index)
            {
                return format_one_arg(std::get<I>(tup), spec_group);
            }
            return format_printf_arg<I + 1>(tup, index, spec_group);
        }
        return "[missing arg]";
    }

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
        case LogLevel::L_ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKWN";
        }
    }
};

// Use macros to automatically capture file and line number
#define LOG_TRACE(fmt, ...) (Logger::get().log_impl(LogLevel::TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_DEBUG(fmt, ...) (Logger::get().log_impl(LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_INFO(fmt, ...) (Logger::get().log_impl(LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_STEP(fmt, ...) (Logger::get().log_impl(LogLevel::STEP, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_WARNING(fmt, ...) (Logger::get().log_impl(LogLevel::WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_ERROR(fmt, ...) (Logger::get().log_impl(LogLevel::L_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_FATAL(fmt, ...) (Logger::get().log_impl(LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
