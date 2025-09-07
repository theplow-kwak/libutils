#include "offset2lba.hpp"
#include <iostream>
#include <string>
#include <system_error>

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
