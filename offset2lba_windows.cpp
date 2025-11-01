#include "offset2lba.hpp"

#ifdef _WIN32

#include <iostream>
#include <vector>
#include <windows.h>
#include <winioctl.h>
#include <system_error>
#include <string>
#include <memory>

// RAII wrapper for HANDLE
struct HandleWrapper
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    HandleWrapper(HANDLE h) : handle(h) {}
    ~HandleWrapper()
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }

    // Prevent copying
    HandleWrapper(const HandleWrapper &) = delete;
    HandleWrapper &operator=(const HandleWrapper &) = delete;

    operator HANDLE() const { return handle; }
};

// Struct to hold disk/volume information
struct VolumeInfo
{
    DWORD ClusterSize;
    DWORD BytesPerSector;
    LARGE_INTEGER PartitionStartOffset;
};

// Forward declarations
VolumeInfo GetVolumeInfo(fs::path &filepath);
LARGE_INTEGER FindLcnFromVcn(HANDLE hFile, LONGLONG vcn);
void CalculateAndPrintLbaInfo(fs::path &filepath, off_t offset, const VolumeInfo &volInfo, LARGE_INTEGER lcn);

void get_lba(fs::path &filepath, off_t offset)
{
    try
    {
        // 1. Get file handle and check offset
        std::wstring widePath = filepath.generic_wstring();
        HandleWrapper fileHandle(CreateFile(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL));
        if (fileHandle.handle == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(GetLastError(), std::system_category(), "Failed to open file");
        }

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(fileHandle, &fileSize))
        {
            throw std::system_error(GetLastError(), std::system_category(), "Failed to get file size");
        }
        if (offset >= fileSize.QuadPart)
        {
            throw std::runtime_error("Offset is beyond the end of the file.");
        }

        // 2. Gather all disk and volume information
        VolumeInfo volInfo = GetVolumeInfo(filepath);

        // 3. Find the Logical Cluster Number (LCN) for the given offset
        LONGLONG vcn = offset / volInfo.ClusterSize;
        LARGE_INTEGER lcn = FindLcnFromVcn(fileHandle, vcn);

        // 4. Calculate and print the final results
        CalculateAndPrintLbaInfo(filepath, offset, volInfo, lcn);
    }
    catch (const std::system_error &e)
    {
        std::cerr << "Error: " << e.what() << " (code: " << e.code() << ")" << std::endl;
        throw;
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

VolumeInfo GetVolumeInfo(fs::path &filepath)
{
    std::wstring volumePathW(MAX_PATH, L'\0');
    if (!GetVolumePathName(filepath.c_str(), volumePathW.data(), static_cast<DWORD>(volumePathW.size())))
    {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to get volume path name");
    }

    volumePathW.resize(wcsnlen_s(volumePathW.data(), MAX_PATH));

    DWORD sectorsPerCluster, bytesPerSector, numberOfFreeClusters, totalNumberOfClusters;
    if (!GetDiskFreeSpace(volumePathW.c_str(), &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters, &totalNumberOfClusters))
    {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to get disk free space");
    }

    std::wstring volumeDevicePathW = L"\\\\.\\";
    volumeDevicePathW += volumePathW.substr(0, 2); // \\.\<drive>:
    HandleWrapper volumeHandle(CreateFile(volumeDevicePathW.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL));
    if (volumeHandle.handle == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to open volume");
    }

    VOLUME_DISK_EXTENTS diskExtents;
    DWORD bytesReturned;
    if (!DeviceIoControl(volumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &diskExtents, sizeof(diskExtents), &bytesReturned, NULL))
    {
        throw std::system_error(GetLastError(), std::system_category(), "IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed");
    }

    return {
        sectorsPerCluster * bytesPerSector,
        bytesPerSector,
        diskExtents.Extents[0].StartingOffset};
}

LARGE_INTEGER FindLcnFromVcn(HANDLE hFile, LONGLONG vcn)
{
    STARTING_VCN_INPUT_BUFFER inputBuffer;
    inputBuffer.StartingVcn.QuadPart = vcn;

    std::vector<char> outputBuffer(sizeof(RETRIEVAL_POINTERS_BUFFER) + 20 * sizeof(RETRIEVAL_POINTERS_BUFFER::Extents));
    RETRIEVAL_POINTERS_BUFFER *retrievalPointers = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER *>(outputBuffer.data());
    DWORD bytesReturned;

    if (!DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS, &inputBuffer, sizeof(inputBuffer), retrievalPointers, outputBuffer.size(), &bytesReturned, NULL))
    {
        throw std::system_error(GetLastError(), std::system_category(), "FSCTL_GET_RETRIEVAL_POINTERS failed");
    }

    if (retrievalPointers->ExtentCount == 0)
    {
        throw std::runtime_error("File has no allocated extents (sparse file?)");
    }

    for (DWORD i = 0; i < retrievalPointers->ExtentCount; ++i)
    {
        LARGE_INTEGER nextVcn = retrievalPointers->Extents[i].NextVcn;
        if (vcn < nextVcn.QuadPart)
        {
            // This is the extent that contains our VCN.
            // The LCN of the extent + the offset from the start of the extent's VCNs.
            LARGE_INTEGER lcn;
            lcn.QuadPart = retrievalPointers->Extents[i].Lcn.QuadPart + (vcn - retrievalPointers->StartingVcn.QuadPart);
            return lcn;
        }
    }

    throw std::runtime_error("Could not find the LCN for the given offset.");
}

std::string to_ansi(const fs::path &path)
{
    std::wstring wide_path = path.wstring();
    std::string ansi_path;
    int requiredSize = WideCharToMultiByte(CP_ACP, 0, wide_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredSize > 0)
    {
        ansi_path.resize(requiredSize - 1);
        WideCharToMultiByte(CP_ACP, 0, wide_path.c_str(), -1, ansi_path.data(), requiredSize, nullptr, nullptr);
    }
    return ansi_path;
}

void CalculateAndPrintLbaInfo(fs::path &filepath, off_t offset, const VolumeInfo &volInfo, LARGE_INTEGER lcn)
{
    LONGLONG offsetInCluster = offset % volInfo.ClusterSize;
    LONGLONG filePhysicalOffset = (lcn.QuadPart * volInfo.ClusterSize) + offsetInCluster;
    LONGLONG diskAbsoluteOffset = volInfo.PartitionStartOffset.QuadPart + filePhysicalOffset;
    LONGLONG absoluteLba = diskAbsoluteOffset / volInfo.BytesPerSector;

    std::cout << "File: " << to_ansi(filepath).c_str() << std::endl;
    std::cout << "Offset: " << offset << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "File System Cluster Size: " << volInfo.ClusterSize << " bytes" << std::endl;
    std::cout << "Disk Sector Size: " << volInfo.BytesPerSector << " bytes" << std::endl;
    std::cout << "Partition Start Offset: " << volInfo.PartitionStartOffset.QuadPart / volInfo.BytesPerSector << " (lba)" << std::endl;
    // std::cout << "File Physical Offset (in volume): " << filePhysicalOffset / volInfo.BytesPerSector << " (lba)" << std::endl;
    std::cout << "Absolute Offset on Disk: " << diskAbsoluteOffset << " (bytes)" << std::endl;
    std::cout << "Absolute LBA on Disk: " << absoluteLba << std::endl;
}

#endif // _WIN32
