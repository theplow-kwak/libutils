#include "offset2lba.hpp"
#include "logger.hpp"
#include <iostream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h> // For WideCharToMultiByte

// Helper to convert wstring to string for the logger
std::string to_string(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
int wmain(int argc, wchar_t *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    if (argc != 3)
    {
#ifdef _WIN32
        LOG_FATAL("Usage: {} <file_path> <offset>", to_string(fs::path(argv[0]).filename().wstring()));
#else
        LOG_FATAL("Usage: {} <file_path> <offset>", fs::path(argv[0]).filename().string());
#endif
        return 1;
    }

    fs::path filepath(argv[1]);
#ifdef _WIN32
    off_t offset = std::stoll(argv[2]);
#else
    off_t offset = std::stoll(argv[2]);
#endif

    try
    {
        get_lba(filepath, offset);
    }
    catch (const std::system_error &e)
    {
        // Errors inside get_lba are already logged. This is for top-level errors.
        LOG_FATAL("A top-level system error occurred: {} (code: {})", e.what(), e.code().value());
        return 1;
    }
    catch (const std::runtime_error &e)
    {
        LOG_FATAL("A top-level runtime error occurred: {}", e.what());
        return 1;
    }
    catch (...)
    {
        LOG_FATAL("An unknown error occurred.");
        return 1;
    }

    return 0;
}
