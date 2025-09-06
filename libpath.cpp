#include <filesystem>
#include <regex>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <optional>
#include <type_traits>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
#include <unistd.h>

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
    fs::path target_dir = fs::current_path();
    bool dry_run = false;
};

Config parse_args(int argc, char **argv)
{
    Config cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--dry-run")
            cfg.dry_run = true;
        else if (a == "-h" || a == "--help")
        {
            std::cout << "Usage: " << (argv[0] ? argv[0] : "libpath") << " [dir] [--dry-run]\n";
            std::exit(0);
        }
        else
            cfg.target_dir = fs::path(a);
    }
    return cfg;
}

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
        for (auto &e : fs::directory_iterator(dir_))
        {
            if (!e.is_regular_file())
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
        // remove trailing "[digits]"
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
    explicit NameTransformer(const Config &cfg) {}

    // 주어진 파일 경로 p와 할당된 번호 assigned에 대해 새 경로를 반환
    fs::path transform(const fs::path &p, int assigned) const
    {
        // 기존 stem에서 모든 후행 태그를 제거한 기반 이름(base)을 사용
        StringT name = path_stem(p);
        StringT base = strip_trailing_number_tags<StringT>(name);
        StringT ext = path_ext(p);
        std::string tag8 = "[" + pad_num(assigned) + "]";
        StringT tag = from_utf8<StringT>(tag8);
        fs::path parent = p.parent_path();
        return parent / fs::path(base + tag + ext);
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
// process_iteration: 한 번의 collect -> transform -> copy 수행, 처리된 파일 수 반환
// 변경: entries/used 제거. paths를 바로 순회하며 각 파일별로 tag+1 또는 cfg.start_num부터 시도.
// ------------------------
template <typename StringT>
int process_iteration(const Config &cfg)
{
    FileList collector(cfg.target_dir);
    auto paths = collector.collect_sorted();

    NameTransformer<StringT> transformer(cfg);
    int processed_count = 0;

    for (const auto &p : paths)
    {
        // 현재 파일의 stem과 기존 tag 확인
        StringT stem = path_stem_generic<StringT>(p);
        auto tag_opt = extract_trailing_number_tag_generic<StringT>(stem);

        // 파일별 시작 번호: 기존 태그가 있으면 tag+1, 없으면 0 으로 고정
        int assigned = tag_opt ? (*tag_opt + 1) : 0;
        fs::path candidate;

        // candidate 가 파일시스템에 없을 때까지 증가
        for (;;)
        {
            candidate = transformer.transform(p, assigned);

            std::error_code ec;
            bool exists = fs::exists(candidate, ec);
            if (ec)
            {
                // I/O 에러: 로그 후 다음 번호 시도
                print_error_msg(std::string("fs::exists error: ") + ec.message());
                ++assigned;
                continue;
            }
            if (!exists)
                break; // 사용 가능한 후보 발견
            ++assigned;
        }

        // 복사 수행 (dry-run이면 출력만)
        print_path_pair(p, candidate);
        if (!cfg.dry_run)
        {
            std::error_code ec;
            fs::copy_file(p, candidate, fs::copy_options::skip_existing, ec);
            if (ec)
            {
                print_error_msg(std::string("copy failed: ") + ec.message());
            }
        }
        ++processed_count;
    }

    return processed_count;
}

// ------------------------
// main: process_iteration을 반복 호출하여 외부 루프 유지
// ------------------------
int main(int argc, char **argv)
{
    Config cfg = parse_args(argc, argv);

    if (!fs::exists(cfg.target_dir) || !fs::is_directory(cfg.target_dir))
    {
        print_error_msg(std::string("Target is not a directory: ") + cfg.target_dir.string());
        return 1;
    }

    // 반복: collect -> per-file transform/copy -> collect again (새로 추가된 파일 포함)
    const int max_iterations = 10; // 안전 장치: 필요 시 조정
    for (int iter = 0; iter < max_iterations; ++iter)
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
        if (processed == 0)
            break; // 종료 조건: 이번 반복에서 처리된 파일이 없음

        std::cout << "Processed files in this iteration: " << processed << "\n\n";
        sleep(5); // 5초 대기
    }

    return 0;
}