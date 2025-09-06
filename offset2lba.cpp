#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <system_error>
#include <cstring>
#include <sys/stat.h> // Include for struct stat
#include <cstdlib>    // Include for malloc/free

// 디스크 섹터 크기 (일반적으로 512 바이트)
constexpr int SECTOR_SIZE = 512;

// 파일 경로와 오프셋을 받아 LBA를 계산하는 함수
void get_lba(const char *filepath, off_t offset)
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Failed to open file");
    }

    // 파일의 논리 블록 크기 가져오기
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        close(fd);
        throw std::system_error(errno, std::generic_category(), "Failed to get file stats");
    }
    long long block_size = st.st_blksize;

    // FIEMAP 구조체 준비
    // extent들을 담을 수 있도록 fiemap + extent 배열 크기만큼 할당한다.
    unsigned int max_extents = 16;
    size_t alloc_size = sizeof(struct fiemap) + max_extents * sizeof(struct fiemap_extent);
    struct fiemap *fiemap_data = (struct fiemap *)malloc(alloc_size);
    if (!fiemap_data)
    {
        close(fd);
        throw std::system_error(errno, std::generic_category(), "Failed to allocate memory for fiemap");
    }
    memset(fiemap_data, 0, alloc_size);

    fiemap_data->fm_start = offset;
    fiemap_data->fm_length = 1; // 오프셋이 포함된 1바이트만 확인
    fiemap_data->fm_flags = FIEMAP_FLAG_SYNC;
    fiemap_data->fm_extent_count = max_extents; // 커널에 요청할 extent 최대 개수

    if (ioctl(fd, FS_IOC_FIEMAP, fiemap_data) < 0)
    {
        close(fd);
        free(fiemap_data);
        throw std::system_error(errno, std::generic_category(), "ioctl(FS_IOC_FIEMAP) failed");
    }

    if (fiemap_data->fm_mapped_extents == 0)
    {
        std::cout << "Offset " << offset << " is not mapped to any physical block (sparse file?)." << std::endl;
        close(fd);
        free(fiemap_data);
        return;
    }

    // 첫 번째 매핑된 extent 사용 (offset을 포함하는 extent가 반환된다고 가정)
    const struct fiemap_extent *extent = &fiemap_data->fm_extents[0];
    unsigned long long physical_block_address_bytes = extent->fe_physical + (unsigned long long)(offset - extent->fe_logical);
    constexpr unsigned long long SECTOR_SIZE = 512;
    unsigned long long lba = physical_block_address_bytes / SECTOR_SIZE;

    std::cout << "File: " << filepath << std::endl;
    std::cout << "Offset: " << offset << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "File System Block Size: " << block_size << " bytes" << std::endl;
    std::cout << "Physical Block Address: " << physical_block_address_bytes << " (bytes)" << std::endl;
    std::cout << "Disk LBA (Logical Block Address): " << lba << std::endl;

    free(fiemap_data);
    close(fd);
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