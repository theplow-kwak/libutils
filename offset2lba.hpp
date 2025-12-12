#ifndef OFFSET2LBA_HPP
#define OFFSET2LBA_HPP

#include <string>
#include <filesystem>
#include <sys/types.h>

namespace fs = std::filesystem;

#ifdef _WIN32
using os_string_t = std::wstring;
#else
using os_string_t = std::string;
#endif

// Calculates the LBA for a given file path and offset.
void get_lba(fs::path &filepath, off_t offset);

#endif // OFFSET2LBA_HPP
