#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <system_error>
#include <sys/stat.h>
#include <string>
#include <utility>         // For std::pair
#include <fstream>         // For std::ifstream
#include <dirent.h>        // For opendir, readdir, closedir
#include <sys/sysmacros.h> // For major(), minor(), makedev()

// RAII wrapper for file descriptor to ensure it's closed.
struct FileDescriptor
{
    int fd;
    FileDescriptor(const char *path, int flags) : fd(open(path, flags)) {}
    ~FileDescriptor()
    {
        if (fd >= 0)
            close(fd);
    }
    operator int() const { return fd; }
};

// 디스크 섹터 크기 (일반적으로 512 바이트)
constexpr int DEFAULT_SECTOR_SIZE = 512;

// Forward declarations
long long get_partition_start_sector(dev_t dev_id);
void calculate_and_print_lba(const struct fiemap *fiemap_data, const struct stat &st, const char *filepath, off_t offset);
std::pair<std::vector<char>, struct stat> get_fiemap_data(const char *filepath, off_t offset);

// Main function to coordinate LBA calculation.
void get_lba(const char *filepath, off_t offset)
{
    auto [fiemap_buffer, st] = get_fiemap_data(filepath, offset);
    const struct fiemap *fiemap_data = reinterpret_cast<const struct fiemap *>(fiemap_buffer.data());
    calculate_and_print_lba(fiemap_data, st, filepath, offset);
}

// Gets fiemap data for a given offset.
std::pair<std::vector<char>, struct stat> get_fiemap_data(const char *filepath, off_t offset)
{
    FileDescriptor fd(filepath, O_RDONLY);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Failed to open file");
    }

    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Failed to get file stats");
    }

    constexpr unsigned int max_extents = 16;
    const size_t alloc_size = sizeof(struct fiemap) + max_extents * sizeof(struct fiemap_extent);
    std::vector<char> fiemap_buffer(alloc_size);
    struct fiemap *fiemap_data = reinterpret_cast<struct fiemap *>(fiemap_buffer.data());

    fiemap_data->fm_start = offset;
    fiemap_data->fm_length = 1;
    fiemap_data->fm_flags = FIEMAP_FLAG_SYNC;
    fiemap_data->fm_extent_count = max_extents;

    if (ioctl(fd, FS_IOC_FIEMAP, fiemap_data) < 0)
    {
        throw std::system_error(errno, std::generic_category(), "ioctl(FS_IOC_FIEMAP) failed");
    }

    return {std::move(fiemap_buffer), st};
}

// Finds the partition start sector by searching /sys/class/block.
long long get_partition_start_sector(dev_t dev_id)
{
    const std::string sys_block_path = "/sys/class/block/";
    DIR *dir = opendir(sys_block_path.c_str());
    if (!dir)
    {
        std::cerr << "Warning: Could not open /sys/class/block to find partition start." << std::endl;
        return 0;
    }

    long long partition_start = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string dev_name = entry->d_name;
        if (dev_name == "." || dev_name == "..")
        {
            continue;
        }

        std::string dev_path = sys_block_path + dev_name + "/dev";
        std::ifstream dev_file(dev_path);
        if (dev_file.is_open())
        {
            unsigned int dev_major, dev_minor;
            char colon;
            dev_file >> dev_major >> colon >> dev_minor;

            if (makedev(dev_major, dev_minor) == dev_id)
            {
                std::string start_path = sys_block_path + dev_name + "/start";
                std::ifstream start_file(start_path);
                if (start_file.is_open())
                {
                    start_file >> partition_start;
                }
                break; // Found device
            }
        }
    }

    closedir(dir);
    return partition_start;
}

// Calculates LBA from fiemap data and prints the results.
void calculate_and_print_lba(const struct fiemap *fiemap_data, const struct stat &st, const char *filepath, off_t offset)
{
    if (fiemap_data->fm_mapped_extents == 0)
    {
        std::cout << "Offset " << offset << " is not mapped to any physical block (sparse file?)." << std::endl;
        return;
    }

    const struct fiemap_extent *extent = &fiemap_data->fm_extents[0];
    unsigned long long physical_block_address_bytes = extent->fe_physical + (static_cast<unsigned long long>(offset) - extent->fe_logical);
    unsigned long long fs_lba = physical_block_address_bytes / DEFAULT_SECTOR_SIZE;

    long long partition_start_lba = get_partition_start_sector(st.st_dev);
    unsigned long long absolute_lba = fs_lba + partition_start_lba;

    std::cout << "File: " << filepath << std::endl;
    std::cout << "Offset: " << offset << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "File System Block Size: " << st.st_blksize << " bytes" << std::endl;
    std::cout << "Physical Block Address: " << physical_block_address_bytes << " (bytes)" << std::endl;
    std::cout << "LBA (relative to filesystem): " << fs_lba << std::endl;
    std::cout << "Partition Start LBA:          " << partition_start_lba << std::endl;
    std::cout << "Absolute LBA on Disk:         " << absolute_lba << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <file_path> <offset>" << std::endl;
        return 1;
    }

    const char *filepath = argv[1];
    off_t offset = std::stoll(argv[2]);

    try
    {
        get_lba(filepath, offset);
    }
    catch (const std::system_error &e)
    {
        std::cerr << "Error: " << e.what() << " (code: " << e.code() << ")" << std::endl;
        return 1;
    }

    return 0;
}