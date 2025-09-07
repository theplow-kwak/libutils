#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <optional>
#include <type_traits>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
using os_string_t = std::wstring;
#else
using os_string_t = std::string;
#endif

#ifdef _WIN32
static os_string_t from_utf8(const std::string &s)
{
    if (s.empty())
        return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (size_needed <= 0)
        return {};
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &wstr[0], size_needed);
    return wstr;
}
#else
static os_string_t from_utf8(const std::string &s)
{
    return s;
}
#endif

static void print_path_pair(const fs::path &oldp, const fs::path &newp)
{
    std::cout << reinterpret_cast<const char *>(oldp.u8string().c_str())
              << " -> "
              << reinterpret_cast<const char *>(newp.u8string().c_str()) << "\n";
}

static void print_error_msg(const std::string &msg)
{
    std::cerr << msg << "\n";
}

static std::string pad_num(int n, int width = 6)
{
    std::ostringstream oss;
    oss << std::setw(width) << std::setfill('0') << n;
    return oss.str();
}

struct Config
{
    fs::path source_dir = fs::current_path();
    fs::path dest_dir;
    bool dry_run = false;
};

static std::vector<fs::path> collect_files(const fs::path &dir)
{
    std::vector<fs::path> paths;
    paths.reserve(128);
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec))
    {
        if (ec)
        {
            print_error_msg("Error iterating source directory: " + ec.message());
            break;
        }
        if (!e.is_regular_file(ec))
            continue;
        paths.push_back(e.path());
    }
    return paths;
}

static std::vector<fs::path> collect_sorted_files(const fs::path &dir)
{
    auto paths = collect_files(dir);
    std::sort(paths.begin(), paths.end(), [](const fs::path &a, const fs::path &b)
              {
#ifdef _WIN32
                  return a.wstring() < b.wstring();
#else
                  return a.string() < b.string();
#endif
              });
    return paths;
}

static std::optional<int> extract_trailing_number_tag(const os_string_t &stem)
{
    if (stem.size() < 3)
        return std::nullopt;
    using char_t = typename os_string_t::value_type;
    if (stem.back() != static_cast<char_t>(']'))
        return std::nullopt;
    auto pos = stem.find_last_of(static_cast<char_t>('['));
    if (pos == os_string_t::npos || pos + 1 >= stem.size() - 1)
        return std::nullopt;
    int start = static_cast<int>(pos) + 1;
    int end = static_cast<int>(stem.size()) - 1;
    if (start >= end)
        return std::nullopt;

    auto start_it = stem.begin() + start;
    auto end_it = stem.begin() + end;
    if (!std::all_of(start_it, end_it, [](char_t c)
                     { return c >= static_cast<char_t>('0') && c <= static_cast<char_t>('9'); }))
    {
        return std::nullopt;
    }

    try
    {
        return std::stoi(stem.substr(start, end - start));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

static os_string_t strip_trailing_number_tags(const os_string_t &s)
{
    if (s.empty())
        return os_string_t{};
    using char_t = typename os_string_t::value_type;
    os_string_t res = s;
    while (res.size() >= 3 && res.back() == static_cast<char_t>(']'))
    {
        auto pos = res.find_last_of(static_cast<char_t>('['));
        if (pos == os_string_t::npos || pos + 1 >= res.size() - 1)
            break;
        auto start_it = res.begin() + pos + 1;
        auto end_it = res.end() - 1;
        if (!std::all_of(start_it, end_it, [](char_t c)
                         { return c >= static_cast<char_t>('0') && c <= static_cast<char_t>('9'); }))
        {
            break;
        }
        res.erase(pos);
    }
    return res;
}

static os_string_t path_stem_generic(const fs::path &p)
{
#ifdef _WIN32
    return p.stem().wstring();
#else
    return p.stem().string();
#endif
}

static os_string_t path_ext_generic(const fs::path &p)
{
#ifdef _WIN32
    return p.has_extension() ? p.extension().wstring() : os_string_t{};
#else
    return p.has_extension() ? p.extension().string() : os_string_t{};
#endif
}

int process_iteration(const Config &cfg)
{
    auto paths = collect_sorted_files(cfg.source_dir);

    std::unordered_set<fs::path> dest_paths;
    if (cfg.source_dir == cfg.dest_dir)
    {
        dest_paths.insert(paths.begin(), paths.end());
    }
    else
    {
        if (fs::exists(cfg.dest_dir))
        {
            auto dest_files = collect_files(cfg.dest_dir);
            dest_paths.insert(dest_files.begin(), dest_files.end());
        }
    }

    int processed_count = 0;

    for (const auto &p : paths)
    {
        os_string_t stem = path_stem_generic(p);
        os_string_t base_stem = strip_trailing_number_tags(stem);
        os_string_t ext = path_ext_generic(p);

        auto tag_opt = extract_trailing_number_tag(stem);
        int assigned = tag_opt ? (*tag_opt + 1) : -1;

        fs::path candidate;
        do
        {
            os_string_t tag;
            if (assigned >= 0)
            {
                tag = from_utf8("[" + pad_num(assigned) + "]");
            }
            candidate = cfg.dest_dir / fs::path(base_stem + tag + ext);
            assigned++;
        } while (dest_paths.count(candidate));

        print_path_pair(p, candidate);
        if (!cfg.dry_run)
        {
            std::error_code ec;
            fs::copy_file(p, candidate, fs::copy_options::skip_existing, ec);
            if (ec)
            {
                std::string u8str(reinterpret_cast<const char *>(p.u8string().c_str()));
                print_error_msg("copy failed for '" + u8str + "': " + ec.message());
            }
        }

        dest_paths.insert(candidate);
        ++processed_count;
    }

    return processed_count;
}

Config parse_args(int argc, char **argv)
{
    Config cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--dry-run")
        {
            cfg.dry_run = true;
        }
        else if (a == "-s" || a == "--source")
        {
            cfg.source_dir = fs::path(argv[++i]);
        }
        else if (a == "-d" || a == "--dest")
        {
            cfg.dest_dir = fs::path(argv[++i]);
        }
        else if (a == "-h" || a == "--help")
        {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "libpath") << " [--source <dir>] [--dest <dir>] [--dry-run]\n";
            std::exit(0);
        }
    }
    return cfg;
}

// ------------------------
// main: process_iteration을 반복 호출하여 외부 루프 유지
// ------------------------
int main(int argc, char **argv)
{
#ifdef _WIN32
    // Windows에서 UTF-8 출력을 활성화합니다.
    SetConsoleOutputCP(CP_UTF8);
#endif

    Config cfg = parse_args(argc, argv);

    if (!fs::exists(cfg.source_dir) || !fs::is_directory(cfg.source_dir))
    {
        std::string u8str(reinterpret_cast<const char *>(cfg.source_dir.u8string().c_str()));
        print_error_msg("Target is not a directory: " + u8str);
        return 1;
    }
    if (cfg.dest_dir.empty())
        cfg.dest_dir = cfg.source_dir;

    if (!fs::exists(cfg.dest_dir))
    {
        std::error_code ec;
        fs::create_directories(cfg.dest_dir, ec);
        if (ec)
        {
            print_error_msg("Failed to create destination directory: " + ec.message());
            return 1;
        }
    }

    static constexpr int MAX_ITERATIONS = 10; // 안전 장치
    static constexpr int SLEEP_SECONDS = 5;   // 반복 간 대기 시간

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter)
    {
        std::cout << "Iteration: " << iter + 1 << "\n";

        int processed = process_iteration(cfg);

        if (processed == 0)
        {
            std::cout << "No files to process. Exiting.\n";
            break;
        }

        std::cout << "Processed files in this iteration: " << processed << "\n\n";
        if (iter < MAX_ITERATIONS - 1)
        {
#ifdef _WIN32
            Sleep(SLEEP_SECONDS * 1000);
#else
            sleep(SLEEP_SECONDS);
#endif
        }
    }

    return 0;
}