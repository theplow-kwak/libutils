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
    LOG_TRACE,
    LOG_DEBUG,
    LOG_STEP,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
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
            {"TRACE", LogLevel::LOG_TRACE},
            {"DEBUG", LogLevel::LOG_DEBUG},
            {"INFO", LogLevel::LOG_INFO},
            {"STEP", LogLevel::LOG_STEP},
            {"WARNING", LogLevel::LOG_WARNING},
            {"ERROR", LogLevel::LOG_ERROR},
            {"FATAL", LogLevel::LOG_FATAL}};

        std::string upper_level = level_str;
        std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(), ::toupper);
        auto it = level_map.find(upper_level);
        if (it != level_map.end())
        {
            level_ = it->second;
        }
        else
        {
            level_ = LogLevel::LOG_INFO;
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
            if (is_printf_style(fmt_str))
            {
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                msg = format_printf(fmt_str, tup);
            }
            else
            {

                msg = std::vformat(fmt_str, std::make_format_args(args...));
            }
        }
        catch (const std::exception &e)
        {
            msg = std::string("Log formatting error: ") + e.what();
            std::cerr << msg << std::endl;
            msg = fmt_str;
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
    Logger() : level_(LogLevel::LOG_INFO) {}
    ~Logger() = default;

    LogLevel level_;
    std::mutex mutex_;
    std::unique_ptr<std::ofstream> file_stream_;

    static bool is_printf_style(std::string_view s)
    {
        for (size_t i = 0; i + 1 < s.size(); ++i)
        {
            if (s[i] == '%')
            {
                if (s[i + 1] == '%')
                {
                    ++i;
                    continue;
                }
                if (std::string_view("diuoxXfFeEgGaAcspn").find(s[i + 1]) != std::string_view::npos)
                    return true;
            }
        }
        return false;
    }

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

        char spec_char = ' ';
        if (!spec_group.empty())
        {
            for (size_t i = spec_group.length() - 1; i > 0; --i)
            {
                if (isalpha(spec_group[i]))
                {
                    spec_char = spec_group[i];
                    break;
                }
            }
        }

        auto format_error = [&]() -> std::string {
            std::ostringstream oss;
            oss << "[FORMAT_ERROR: Mismatch between " << fmt << " and argument type]";
            return oss.str();
        };

        switch (spec_char)
        {
        case 's':
            if constexpr (std::is_convertible_v<std::decay_t<T>, const char *>)
                snprintf(buffer, sizeof(buffer), fmt.c_str(), v);
            else if constexpr (std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>)
                snprintf(buffer, sizeof(buffer), fmt.c_str(), std::string(v).c_str());
            else
                return format_error();
            break;

        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if constexpr (std::is_integral_v<T> || std::is_same_v<T, bool>)
                snprintf(buffer, sizeof(buffer), fmt.c_str(), static_cast<long long>(v));
            else
                return format_error();
            break;

        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            if constexpr (std::is_floating_point_v<T>)
                snprintf(buffer, sizeof(buffer), fmt.c_str(), static_cast<double>(v));
            else
                return format_error();
            break;

        case 'c':
            if constexpr (std::is_integral_v<T>) // char is promoted to int
                snprintf(buffer, sizeof(buffer), fmt.c_str(), static_cast<int>(v));
            else
                return format_error();
            break;

        case 'p':
            if constexpr (std::is_pointer_v<T>)
                snprintf(buffer, sizeof(buffer), fmt.c_str(), v);
            else
                return format_error();
            break;

        default:
            return format_error();
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
        case LogLevel::LOG_TRACE:
            return "TRACE";
        case LogLevel::LOG_DEBUG:
            return "DEBUG";
        case LogLevel::LOG_INFO:
            return "INFO ";
        case LogLevel::LOG_STEP:
            return "STEP ";
        case LogLevel::LOG_WARNING:
            return "WARN ";
        case LogLevel::LOG_ERROR:
            return "ERROR";
        case LogLevel::LOG_FATAL:
            return "FATAL";
        default:
            return "UNKWN";
        }
    }
};

// Use macros to automatically capture file and line number
#define LOG_TRACE(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_DEBUG(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_INFO(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_STEP(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_STEP, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_WARNING(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_ERROR(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#define LOG_FATAL(fmt, ...) (Logger::get().log_impl(LogLevel::LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__))
