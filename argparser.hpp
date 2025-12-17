#pragma once

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace argparse
{
    class ArgParser
    {
    private:
        // Option structure for named arguments
        struct Option
        {
            std::string long_name;
            std::string short_name;
            std::string help;
            std::optional<std::string> default_value;
            std::optional<std::string> value;
            bool required;
            bool is_flag; // True if it's a flag, false if it's an option with a value
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

    public:
        ArgParser(const std::string &desc = "") : description_(desc) {}

        void set_description(const std::string &desc) { description_ = desc; }

        void add_option(const std::string &long_name, const std::string &short_name = "", const std::string &help = "", bool required = false, const std::string &default_value = "")
        {
            size_t index = options_.size();
            options_.push_back({long_name, short_name, help, default_value.empty() ? std::nullopt : std::make_optional(default_value), std::nullopt, required, false});
            if (!long_name.empty())
                option_map_[strip(long_name)] = index;
            if (!short_name.empty())
                option_map_[strip(short_name)] = index;
        }

        void add_flag(const std::string &long_name, const std::string &short_name = "", const std::string &help = "")
        {
            size_t index = options_.size();
            options_.push_back({long_name, short_name, help, std::nullopt, std::nullopt, false, true});
            if (!long_name.empty())
                option_map_[strip(long_name)] = index;
            if (!short_name.empty())
                option_map_[strip(short_name)] = index;
        }

        // Add a positional argument with optional help, required flag, and default value
        void add_positional(const std::string &name, const std::string &help = "", bool required = false, const std::string &default_value = "")
        {
            positional_defs_.emplace_back(Positional{name, help, required, std::nullopt, default_value.empty() ? std::nullopt : std::make_optional(default_value)});
        }

        // Parse command line arguments. Returns true if parsing is successful, false otherwise.
        bool parse(int argc, char *argv[])
        {
            size_t pos_idx = 0;
            for (int i = 1; i < argc; ++i)
            {
                std::string_view arg = argv[i];
                if (arg == "--help" || arg == "-h")
                {
                    print_help(argv[0]);
                    return false; // Early exit for help
                }

                if (arg[0] == '-')
                {
                    std::string_view arg_name = arg;
                    std::optional<std::string_view> arg_val;

                    if (size_t equals_pos = arg.find('='); equals_pos != std::string_view::npos)
                    {
                        arg_name = arg.substr(0, equals_pos);
                        arg_val = arg.substr(equals_pos + 1);
                    }

                    auto it = option_map_.find(strip(arg_name));
                    if (it != option_map_.end())
                    {
                        Option &opt = options_[it->second];
                        if (opt.is_flag)
                        {
                            opt.value = "true";
                        }
                        else // Option with a value
                        {
                            if (arg_val.has_value())
                            {
                                opt.value = std::string(*arg_val);
                            }
                            else if (i + 1 < argc)
                            {
                                opt.value = argv[++i];
                            }
                            else
                            {
                                std::cerr << "Option '" << arg_name << "' requires a value.\n";
                                print_help(argv[0]);
                                return false;
                            }
                        }
                    }
                    else
                    {
                        std::cerr << "Unknown option: " << arg_name << "\n";
                        print_help(argv[0]);
                        return false;
                    }
                }
                else
                {
                    if (pos_idx < positional_defs_.size())
                    {
                        positional_defs_[pos_idx++].value = std::string(arg);
                    }
                    else
                    {
                        positional_args_.push_back(std::string(arg));
                    }
                }
            }
            // Set default values for options if not set
            for (auto &opt : options_)
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
            // Check required options
            for (const auto &opt : options_)
            {
                if (opt.required && !opt.value.has_value())
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

        template <typename T>
        std::optional<T> get(const std::string &name) const
        {
            auto value_str_opt = get(name); // Calls the std::string version
            if (!value_str_opt)
            {
                return std::nullopt;
            }

            const std::string& value_str = *value_str_opt;
            T val;

            if constexpr (std::is_same_v<T, bool>)
            {
                std::string lower_str = value_str;
                std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                               [](unsigned char c)
                               { return std::tolower(c); });
                if (lower_str == "true" || lower_str == "1")
                    return true;
                if (lower_str == "false" || lower_str == "0")
                    return false;
                return std::nullopt;
            }
            else
            {
                std::istringstream iss(value_str);
                if constexpr (std::is_integral_v<T>)
                {
                    // Check for hex prefix or hex characters
                    bool is_hex = (value_str.size() > 2 && value_str[0] == '0' &&
                                   (value_str[1] == 'x' || value_str[1] == 'X')) ||
                                  std::any_of(value_str.begin(), value_str.end(),
                                              [](char c)
                                              { return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });

                    if (is_hex)
                        iss >> std::hex >> val;
                    else
                        iss >> val;
                }
                else
                { // Floating point or other non-bool, non-integral types
                    iss >> val;
                }

                if (iss.fail() || iss.peek() != EOF)
                {
                    return std::nullopt;
                }
                return val;
            }
        }

        std::optional<std::string> get(const std::string &name) const
        {
            auto it = option_map_.find(name);
            if (it != option_map_.end())
            {
                const auto &opt = options_[it->second];
                if (opt.value.has_value())
                    return opt.value;
            }
            return std::nullopt;
        }

        std::optional<std::vector<std::string>> get_list(const std::string &name, char delimiter = ',') const
        {
            auto value_opt = get(name);
            if (!value_opt)
            {
                return std::nullopt;
            }
            std::vector<std::string> tokens;
            std::string token;
            std::istringstream tokenStream(*value_opt);
            while (std::getline(tokenStream, token, delimiter))
            {
                if (!token.empty())
                {
                    tokens.push_back(token);
                }
            }
            return tokens;
        }

        bool is_set(const std::string &name) const
        {
            auto it = option_map_.find(name);
            if (it != option_map_.end())
            {
                return options_[it->second].value.has_value();
            }
            return false;
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
            size_t maxlen = 25;
            if (!positional_defs_.empty())
            {
                std::cout << "Positional arguments:\n";
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
                std::cout << "\n";
            }
            // --- Options & Flags ---
            std::cout << "Options:\n";
            std::vector<std::pair<std::string, std::string>> all_list;

            for (const auto &opt : options_)
            {
                std::string optstr;
                if (!opt.short_name.empty())
                    optstr += opt.short_name + ", ";
                optstr += opt.long_name;
                if (!opt.is_flag)
                    optstr += " <value>";

                std::string desc = opt.help;
                if (opt.required)
                    desc += " (required)";
                if (opt.default_value.has_value())
                    desc += " [default: " + *opt.default_value + "]";

                all_list.emplace_back(optstr, desc);
                maxlen = std::max(maxlen, optstr.size());
            }

            // Print all options and flags
            for (const auto &p : all_list)
            {
                std::cout << "  " << std::left << std::setw(static_cast<int>(maxlen) + 2) << p.first << p.second << "\n";
            }
        }

    private:
        static std::string strip(std::string_view s)
        {
            if (s.rfind("--", 0) == 0)
                return std::string(s.substr(2));
            if (s.rfind("-", 0) == 0)
                return std::string(s.substr(1));
            return std::string(s);
        }

        std::string description_;
        std::vector<Option> options_;
        std::unordered_map<std::string, size_t> option_map_;
        std::vector<Positional> positional_defs_;
        std::vector<std::string> positional_args_;
    };
}