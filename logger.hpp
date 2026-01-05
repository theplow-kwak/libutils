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
        if (is_printf_style(fmt_str))
        {
            // Format printf-style by walking the argument tuple and converting each arg to string
            auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
            msg = format_printf(fmt_str, tup);
        }
        else
        {
            // std::vformat works for std::format-style {} formats
            msg = std::vformat(fmt_str, std::make_format_args(args...));
        }

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
    Logger() : level_(LogLevel::INFO) {}
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
                if (s[i + 1] == '%') { ++i; continue; }
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
        for (size_t i = 0; i < fmt.size(); ++i)
        {
            if (fmt[i] == '%')
            {
                if (i + 1 < fmt.size() && fmt[i + 1] == '%')
                {
                    os << '%';
                    ++i;
                    continue;
                }
                // find conversion specifier char
                size_t j = i + 1;
                while (j < fmt.size() && !std::isalpha(static_cast<unsigned char>(fmt[j])) && fmt[j] != '%')
                    ++j;
                if (j >= fmt.size())
                    break;
                char spec = fmt[j];
                os << format_printf_arg(tup, arg_index, spec);
                ++arg_index;
                i = j;
            }
            else
            {
                os << fmt[i];
            }
        }
        return os.str();
    }

    template <std::size_t I = 0, typename Tuple>
    static std::string format_printf_arg(Tuple &tup, size_t index, char spec)
    {
        if constexpr (I < std::tuple_size_v<std::remove_reference_t<Tuple>>)
        {
            if (I == index)
                return format_one(std::get<I>(tup), spec);
            return format_printf_arg<I + 1>(tup, index, spec);
        }
        return std::string("[missing arg]");
    }

    template <typename T>
    static std::string format_one(const T &v, char spec)
    {
        std::ostringstream oss;
        if (spec == 's')
        {
            if constexpr (std::is_convertible_v<T, std::string>)
                oss << v;
            else if constexpr (std::is_pointer_v<T> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>)
                oss << (v ? v : "(null)");
            else
                oss << v;
        }
        else if (spec == 'd' || spec == 'i')
        {
            if constexpr (std::is_integral_v<T>)
                oss << static_cast<long long>(v);
            else
                oss << v;
        }
        else if (spec == 'u')
        {
            if constexpr (std::is_integral_v<T>)
                oss << static_cast<std::make_unsigned_t<T>>(v);
            else
                oss << v;
        }
        else if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A')
        {
            if constexpr (std::is_floating_point_v<T>)
                oss << v;
            else
                oss << v;
        }
        else if (spec == 'c')
        {
            if constexpr (std::is_integral_v<T>)
                oss << static_cast<char>(v);
            else if constexpr (std::is_same_v<T, char>)
                oss << v;
            else
                oss << v;
        }
        else if (spec == 'x' || spec == 'X' || spec == 'o')
        {
            if constexpr (std::is_integral_v<T>)
            {
                std::ostringstream tmp;
                if (spec == 'o')
                    tmp << std::oct << v;
                else
                {
                    if (spec == 'X')
                        tmp << std::uppercase;
                    tmp << std::hex << static_cast<std::make_unsigned_t<T>>(v);
                }
                oss << tmp.str();
            }
            else
                oss << v;
        }
        else if (spec == 'p')
        {
            if constexpr (std::is_pointer_v<T>)
                oss << v;
            else
                oss << v;
        }
        else
        {
            oss << v;
        }
        return oss.str();
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
