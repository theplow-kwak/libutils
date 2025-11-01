#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <windows.h>

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " [r/w] [disk_number] [lba] [size]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    int diskNumber = std::stoi(argv[2]);
    long size = std::stol(argv[3]);
    long long lba = std::stoll(argv[4]);

    std::string diskPath = "\\\\.\\PhysicalDrive" + std::to_string(diskNumber);

    HANDLE hDevice = CreateFile(diskPath.c_str(), mode == "r" ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Failed to open disk: " << diskPath << std::endl;
        return 1;
    }

    const long long sectorSize = 512;
    long long offset = lba * sectorSize;

    DWORD bytesRead, bytesWritten;
    std::vector<char> buffer(size, 'A'); // Write arbitrary data (all 'A's in this example)

    LARGE_INTEGER liOffset;
    liOffset.QuadPart = offset;

    if (mode == "r")
    {
        if (!SetFilePointerEx(hDevice, liOffset, NULL, FILE_BEGIN))
        {
            std::cerr << "Failed to set file pointer for reading." << std::endl;
            CloseHandle(hDevice);
            return 1;
        }

        if (!ReadFile(hDevice, buffer.data(), static_cast<DWORD>(size), &bytesRead, NULL))
        {
            std::cerr << "Failed to read from disk: " << diskPath << std::endl;
            CloseHandle(hDevice);
            return 1;
        }

        std::cout << "Read " << static_cast<DWORD>(size) << " bytes from LBA " << static_cast<DWORD>(lba) << std::endl;
        // Print 16 bytes of data by hexdump
        for (size_t i = 0; i < 32; i += 16)
        {
            std::cout << std::hex << std::setw(8) << std::setfill('0') << i << "  ";
            for (size_t j = 0; j < 16 && i + j < buffer.size(); ++j)
            {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(buffer[i + j])) << " ";
            }
            std::cout << "  ";
            for (size_t j = 0; j < 16 && i + j < buffer.size(); ++j)
            {
                char c = buffer[i + j];
                std::cout << (std::isprint(c) ? c : '.');
            }
            std::cout << std::endl;
        }
    }
    else if (mode == "w")
    {
        if (!SetFilePointerEx(hDevice, liOffset, NULL, FILE_BEGIN))
        {
            std::cerr << "Failed to set file pointer for writing." << std::endl;
            CloseHandle(hDevice);
            return 1;
        }

        if (!WriteFile(hDevice, buffer.data(), static_cast<DWORD>(size), &bytesWritten, NULL))
        {
            std::cerr << "Failed to write to disk: " << diskPath << std::endl;
            CloseHandle(hDevice);
            return 1;
        }

        std::cout << "Wrote " << static_cast<DWORD>(size) << " bytes to LBA " << static_cast<DWORD>(lba) << std::endl;
    }
    else
    {
        std::cerr << "Invalid mode: " << mode << ". Use 'r' for read or 'w' for write." << std::endl;
        return 1;
    }

    CloseHandle(hDevice);
    return 0;
}