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

// UTF-8 -> StringT 변환 헬퍼 (StringT: std::string or std::wstring)
#ifdef _WIN32
static std::wstring utf8_to_wstring(const std::string &s)
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
#endif

template <typename StringT>
static StringT from_utf8(const std::string &s)
{
    if constexpr (std::is_same_v<StringT, std::wstring>)
    {
#ifdef _WIN32
        return utf8_to_wstring(s);
#else
        // Non-Windows: Assume locale is UTF-8 compatible or direct conversion is fine.
        return StringT{s.begin(), s.end()};
#endif
    }
    else
    {
        return s;
    }
}

static void print_path_pair(const fs::path &oldp, const fs::path &newp)
{
#ifdef _WIN32
    std::wcout << oldp.wstring() << L" -> " << newp.wstring() << L'\n';
#else
    std::cout << oldp.string() << " -> " << newp.string() << '\n';
#endif
}
static void print_error_msg(const std::string &msg)
{
#ifdef _WIN32
    std::wstring w = utf8_to_wstring(msg);
    std::wcerr << w << L'\n';
#else
    std::cerr << msg << '\n';
#endif
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

// ------------------------
// FileList 클래스: 파일 수집 및 정렬 책임
// ------------------------
class FileList
{
public:
    explicit FileList(const fs::path &dir) : dir_(dir) {}

    std::vector<fs::path> collect_sorted() const
    {
        std::vector<fs::path> paths;
        paths.reserve(128);
        std::error_code ec;
        for (auto &e : fs::directory_iterator(dir_, ec))
        {
            if (ec) {
                print_error_msg("Error iterating source directory: " + ec.message());
                break;
            }
            if (!e.is_regular_file(ec))
                continue;
            paths.push_back(e.path());
        }

        // 결정론적 정렬 (OS별 native 비교)
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

private:
    fs::path dir_;
};

// ------------------------
// extract_trailing_number_tag: 플랫폼 독립 템플릿 함수
// ------------------------
template <typename StringT>
static std::optional<int> extract_trailing_number_tag_generic(const StringT &stem)
{
    if (stem.size() < 3)
        return std::nullopt;
    using char_t = typename StringT::value_type;
    if (stem.back() != static_cast<char_t>(']'))
        return std::nullopt;
    auto pos = stem.find_last_of(static_cast<char_t>('['));
    if (pos == StringT::npos || pos + 1 >= stem.size() - 1)
        return std::nullopt;
    int start = static_cast<int>(pos) + 1;
    int end = static_cast<int>(stem.size()) - 1;
    if (start >= end)
        return std::nullopt;
    for (int i = start; i < end; ++i)
    {
        char_t c = stem[i];
        if (c < static_cast<char_t>('0') || c > static_cast<char_t>('9'))
            return std::nullopt;
    }
    try
    {
        if constexpr (std::is_same_v<StringT, std::wstring>)
            return std::stoi(std::wstring(stem.substr(start, end - start)));
        else
            return std::stoi(std::string(stem.substr(start, end - start)));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// 새 헬퍼: stem에서 모든 후행 "[숫자]" 태그들을 제거하고 기반 이름을 반환
template <typename StringT>
static StringT strip_trailing_number_tags(const StringT &s)
{
    if (s.empty())
        return StringT{};
    using char_t = typename StringT::value_type;
    StringT res = s;
    while (res.size() >= 3 && res.back() == static_cast<char_t>(']'))
    {
        auto pos = res.find_last_of(static_cast<char_t>('['));
        if (pos == StringT::npos || pos + 1 >= res.size() - 1)
            break;
        
        bool all_digits = true;
        for (size_t i = pos + 1; i < res.size() - 1; ++i)
        {
            char_t c = res[i];
            if (c < static_cast<char_t>('0') || c > static_cast<char_t>('9'))
            {
                all_digits = false;
                break;
            }
        }
        if (!all_digits)
            break;
        
        res.erase(pos);
    }
    return res;
}

// ------------------------
// NameTransformer: 단일 파일의 새로운 이름 문자열만 생성 (rename 하지 않음)
// ------------------------
template <typename StringT>
class NameTransformer
{
public:
    explicit NameTransformer(fs::path dir) : dir_(dir) {}

    fs::path transform(const fs::path &p, int assigned) const
    {
        StringT name = path_stem(p);
        StringT base = strip_trailing_number_tags<StringT>(name);
        StringT ext = path_ext(p);
        
        StringT tag;
        if (assigned >= 0) {
            tag = from_utf8<StringT>("[" + pad_num(assigned) + "]");
        }
        
        return dir_ / fs::path(base + tag + ext);
    }

private:
    static StringT path_stem(const fs::path &p)
    {
        if constexpr (std::is_same_v<StringT, std::wstring>)
            return p.stem().wstring();
        else
            return p.stem().string();
    }
    static StringT path_ext(const fs::path &p)
    {
        if constexpr (std::is_same_v<StringT, std::wstring>)
            return p.has_extension() ? p.extension().wstring() : StringT{};
        else
            return p.has_extension() ? p.extension().string() : StringT{};
    }
    fs::path dir_;
};

// ------------------------
// 유틸: path의 stem을 StringT로 반환
// ------------------------
template <typename StringT>
static StringT path_stem_generic(const fs::path &p)
{
    if constexpr (std::is_same_v<StringT, std::wstring>)
        return p.stem().wstring();
    else
        return p.stem().string();
}

// ------------------------
// process_iteration: 최적화된 파일 처리 로직
// ------------------------
template <typename StringT>
int process_iteration(const Config &cfg)
{
    // 목적지 디렉토리의 파일 목록을 미리 스캔하여 메모리 내 세트에 저장 (성능 최적화)
    std::unordered_set<fs::path> dest_paths;
    if (fs::exists(cfg.dest_dir)) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(cfg.dest_dir, ec)) {
            if (ec) {
                print_error_msg("Failed to iterate destination directory: " + ec.message());
                break;
            }
            if (entry.is_regular_file(ec)) {
                dest_paths.insert(entry.path());
            }
        }
    }

    FileList collector(cfg.source_dir);
    auto paths = collector.collect_sorted();

    NameTransformer<StringT> transformer(cfg.dest_dir);
    int processed_count = 0;

    for (const auto &p : paths)
    {
        StringT stem = path_stem_generic<StringT>(p);
        auto tag_opt = extract_trailing_number_tag_generic<StringT>(stem);
        int assigned = tag_opt ? (*tag_opt + 1) : 0;

        fs::path candidate;
        if (tag_opt) {
            // 원본에 태그가 있으면, 다음 번호부터 바로 탐색 시작
            candidate = transformer.transform(p, assigned++);
        } else {
            // 태그가 없으면, 태그 없는 기본 이름부터 확인
            candidate = transformer.transform(p, -1);
        }

        // fs::exists 대신 메모리 내 세트를 사용하여 충돌 확인
        while (dest_paths.count(candidate)) {
            candidate = transformer.transform(p, assigned++);
        }

        print_path_pair(p, candidate);
        if (!cfg.dry_run)
        {
            std::error_code ec;
            fs::copy_file(p, candidate, fs::copy_options::skip_existing, ec);
            if (ec)
            {
                print_error_msg(std::string("copy failed for '" ) + p.string() + "': " + ec.message());
            }
        }
        
        // 현재 처리에서 사용된 이름을 세트에 추가하여 중복 방지
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
        if (a == "--dry-run") {
            cfg.dry_run = true;
        } else if (a == "-s" || a == "--source") {
            if (i + 1 < argc) {
                cfg.source_dir = fs::path(argv[++i]);
            } else {
                print_error_msg("Error: --source requires an argument.");
                std::exit(1);
            }
        } else if (a == "-d" || a == "--dest") {
            if (i + 1 < argc) {
                cfg.dest_dir = fs::path(argv[++i]);
            } else {
                print_error_msg("Error: --dest requires an argument.");
                std::exit(1);
            }
        } else if (a == "-h" || a == "--help") {
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
    Config cfg = parse_args(argc, argv);

    if (!fs::exists(cfg.source_dir) || !fs::is_directory(cfg.source_dir))
    {
        print_error_msg(std::string("Target is not a directory: ") + cfg.source_dir.string());
        return 1;
    }
    if (cfg.dest_dir.empty())
        cfg.dest_dir = cfg.source_dir;
    
    if (!fs::exists(cfg.dest_dir)) {
        std::error_code ec;
        fs::create_directories(cfg.dest_dir, ec);
        if (ec) {
            print_error_msg("Failed to create destination directory: " + ec.message());
            return 1;
        }
    }

    static constexpr int MAX_ITERATIONS = 10; // 안전 장치
    static constexpr int SLEEP_SECONDS = 5;   // 반복 간 대기 시간

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter)
    {
#ifdef _WIN32
        std::wcout << L"Iteration: " << iter + 1 << L'\n';
#else
        std::cout << "Iteration: " << iter + 1 << '\n';
#endif
        int processed = 0;
        if constexpr (std::is_same_v<os_string_t, std::wstring>)
        {
            processed = process_iteration<std::wstring>(cfg);
        }
        else
        {
            processed = process_iteration<std::string>(cfg);
        }
        if (processed == 0) {
            std::cout << "No files to process. Exiting.\n";
            break;
        }

        std::cout << "Processed files in this iteration: " << processed << "\n\n";
        if (iter < MAX_ITERATIONS - 1) {
#ifdef _WIN32
            Sleep(SLEEP_SECONDS * 1000);
#else
            sleep(SLEEP_SECONDS);
#endif
        }
    }

    return 0;
}
