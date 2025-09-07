#ifndef OFFSET2LBA_HPP
#define OFFSET2LBA_HPP

#include <sys/types.h>

// Calculates the LBA for a given file path and offset.
void get_lba(const char *filepath, off_t offset);

#endif // OFFSET2LBA_HPP
