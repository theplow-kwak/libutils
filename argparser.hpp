#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <unordered_set>
#include <iomanip>

class ArgParser
{
public:
    ArgParser(const std::string &desc = "") : description_(desc) {}

    void set_description(const std::string &desc) { description_ = desc; }

    void add_option(const std::string &long_name, const std::string &short_name = "", const std::string &help = "", bool required = false, const std::string &default_value = "")
    {
        Option opt{help, required, std::nullopt, long_name, short_name, default_value.empty() ? std::nullopt : std::make_optional(default_value)};
        if (!long_name.empty())
            option_map_[long_name] = opt;
        if (!short_name.empty())
            option_map_[short_name] = opt;
    }

    void add_flag(const std::string &long_name, const std::string &short_name = "", const std::string &help = "")
    {
        if (!long_name.empty())
            flag_map_[long_name] = help;
        if (!short_name.empty())
            flag_map_[short_name] = help;
    }

    // Add a positional argument with optional help, required flag, and default value
    void add_positional(const std::string &name, const std::string &help = "", bool required = false, const std::string &default_value = "")
    {
        positional_defs_.emplace_back(Positional{name, help, required, default_value.empty() ? std::nullopt : std::make_optional(default_value)});
    }

    // Parse command line arguments. Returns true if parsing is successful, false otherwise.
    bool parse(int argc, char *argv[])
    {
        size_t pos_idx = 0;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h")
            {
                // Print help and exit immediately if help option is provided
                print_help(argv[0]);
                return false;
            }
            if (flag_map_.count(arg))
            {
                parsed_flags_.insert(arg);
            }
            else if (option_map_.count(arg))
            {
                if (i + 1 < argc)
                {
                    option_map_[arg].value = argv[++i];
                    if (!option_map_[arg].long_name.empty())
                        option_map_[option_map_[arg].long_name].value = option_map_[arg].value;
                    if (!option_map_[arg].short_name.empty())
                        option_map_[option_map_[arg].short_name].value = option_map_[arg].value;
                }
                else
                {
                    std::cerr << "Option '" << arg << "' requires a value.\n";
                    print_help(argv[0]);
                    return false;
                }
            }
            else
            {
                if (pos_idx < positional_defs_.size())
                {
                    positional_defs_[pos_idx++].value = arg;
                }
                else
                {
                    positional_args_.push_back(arg);
                }
            }
        }
        // Set default values for options if not set
        for (auto &[name, opt] : option_map_)
        {
            if (!opt.value.has_value() && opt.default_value.has_value())
                opt.value = opt.default_value;
        }
        // Set default values for positional arguments if not set
        for (auto &pos : positional_defs_)
        {
            if (!pos.value.has_value() && pos.default_value.has_value())
                pos.value = pos.default_value;
        }
        // Check required options (only long name)
        for (const auto &[name, opt] : option_map_)
        {
            if (!opt.long_name.empty() && opt.required && !opt.value.has_value())
            {
                std::cerr << "Missing required option: " << opt.long_name << "\n\n";
                print_help(argv[0]);
                return false;
            }
        }
        // Check required positional arguments
        for (const auto &pos : positional_defs_)
        {
            if (pos.required && !pos.value.has_value())
            {
                std::cerr << "Missing required positional argument: " << pos.name << "\n\n";
                print_help(argv[0]);
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> get(const std::string &name) const
    {
        // '--', '-' 접두어 없이도 찾을 수 있도록 처리
        auto match = [&](const std::string &key, const Option &opt)
        {
            if (key == name)
                return true;
            if (opt.long_name == name || opt.short_name == name)
                return true;
            // 접두어 제거 후 비교
            auto strip = [](const std::string &s)
            {
                if (s.rfind("--", 0) == 0)
                    return s.substr(2);
                if (s.rfind("-", 0) == 0)
                    return s.substr(1);
                return s;
            };
            return strip(opt.long_name) == name || strip(opt.short_name) == name || strip(key) == name;
        };
        for (const auto &[k, v] : option_map_)
        {
            if (match(k, v))
            {
                if (v.value.has_value())
                    return v.value;
                if (v.default_value.has_value())
                    return v.default_value;
            }
        }
        return std::nullopt;
    }

    bool is_set(const std::string &name) const
    {
        return parsed_flags_.count(name) > 0;
    }

    const std::vector<std::string> &positional() const
    {
        return positional_args_;
    }

    // Get positional argument value by name (returns default if not set)
    std::optional<std::string> get_positional(const std::string &name) const
    {
        for (const auto &pos : positional_defs_)
        {
            if (pos.name == name)
            {
                if (pos.value.has_value())
                    return pos.value;
                if (pos.default_value.has_value())
                    return pos.default_value;
            }
        }
        return std::nullopt;
    }

    // Print help message for usage and arguments
    void print_help(const std::string &prog_name) const
    {
        std::cout << "Usage: " << prog_name;
        for (const auto &pos : positional_defs_)
        {
            std::cout << " <" << pos.name << ">";
        }
        std::cout << " [options] [args...]\n";
        if (!description_.empty())
            std::cout << description_ << "\n\n";
        // Print positional arguments
        if (!positional_defs_.empty())
        {
            std::cout << "Positional arguments:\n";
            size_t maxlen = 0;
            for (const auto &pos : positional_defs_)
                maxlen = std::max(maxlen, pos.name.size());
            for (const auto &pos : positional_defs_)
            {
                std::cout << "  " << std::left << std::setw(static_cast<int>(maxlen) + 2) << pos.name
                          << pos.help;
                if (pos.required)
                    std::cout << " (required)";
                if (pos.default_value.has_value())
                    std::cout << " [default: " << *pos.default_value << "]";
                std::cout << "\n";
            }
        }
        // --- Options & Flags ---
        std::cout << "Options:\n";
        std::unordered_set<std::string> printed;
        std::vector<std::pair<std::string, std::string>> all_list;
        size_t maxlen = 0;
        // Options
        for (const auto &[name, opt] : option_map_)
        {
            if (!opt.long_name.empty() && printed.insert(opt.long_name).second)
            {
                std::string optstr = (opt.short_name.empty() ? "" : opt.short_name + ", ") + opt.long_name + " <value>";
                std::string desc = opt.help;
                if (opt.required)
                    desc += " (required)";
                if (opt.default_value.has_value())
                    desc += " [default: " + *opt.default_value + "]";
                all_list.emplace_back(optstr, desc);
                maxlen = std::max(maxlen, optstr.size());
            }
        }
        // Flags
        printed.clear();
        for (const auto &[name, help] : flag_map_)
        {
            if (name.size() == 2 && name[0] == '-' && printed.insert(name).second)
                continue;
            std::string short_flag;
            for (const auto &[n, h] : flag_map_)
            {
                if (n.size() == 2 && n[0] == '-' && h == help)
                    short_flag = n;
            }
            std::string flagstr = (short_flag.empty() ? "" : short_flag + ", ") + name;
            all_list.emplace_back(flagstr, help);
            maxlen = std::max(maxlen, flagstr.size());
        }
        // Print all options and flags
        for (const auto &p : all_list)
        {
            std::cout << "  " << std::left << std::setw(static_cast<int>(maxlen) + 2) << p.first << p.second << "\n";
        }
    }

private:
    // Option structure for named arguments
    struct Option
    {
        std::string help;
        bool required;
        std::optional<std::string> value;
        std::string long_name;
        std::string short_name;
        std::optional<std::string> default_value;
    };
    // Positional argument structure
    struct Positional
    {
        std::string name;
        std::string help;
        bool required;
        std::optional<std::string> value;
        std::optional<std::string> default_value;
    };
    std::string description_;
    std::unordered_map<std::string, Option> option_map_;
    std::unordered_map<std::string, std::string> flag_map_;
    std::vector<std::string> positional_args_;
    std::unordered_set<std::string> parsed_flags_;
    std::vector<Positional> positional_defs_;
};

std::vector<std::string> split(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}
