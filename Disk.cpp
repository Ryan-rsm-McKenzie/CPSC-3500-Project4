// CPSC 3500:  A "virtual" Disk
// This implements a simulated disk consisting of an array of blocks.

#include <cstdint>  // intmax_t
#include <cstdlib>
#include <iostream>  // cerr, endl

#include "Disk.h"
#include "Blocks.h"


namespace
{
	using ssize_t = std::intmax_t;
}


// Opens the file "file_name" that represents the disk.  If the file does
// not exist, file is created. Returns true if a file is created and false if
// the file parameter fd exists. Any other error aborts the program.
bool Disk::mount(const char* file_name)
{
	_file.open(file_name, std::ios_base::in | std::ios_base::out);
	if (!_file.is_open()) {
		_file.open(file_name, std::ios_base::app);
		_file.close();
		_file.open(file_name, std::ios_base::in | std::ios_base::out);
		if (!_file.is_open()) {
			std::cerr << "Could not create disk" << std::endl;
			exit(-1);
		}
	}

	return true;
}


// Closes the file descriptor that represents the disk.
void Disk::unmount()
{
	_file.close();
}


// Reads disk block block_num from the disk into block.
void Disk::read_block(int block_num, void *block)
{
	off_t offset;

	if (block_num < 0 || block_num >= NUM_BLOCKS) {
		std::cerr << "Invalid block size" << std::endl;
		exit(-1);
	}

	offset = block_num * BLOCK_SIZE;
	_file.seekp(offset);
	if (!_file.good()) {
		std::cerr << "Seek failure" << std::endl;
		exit(-1);
	}

	_file.read(reinterpret_cast<char*>(block), BLOCK_SIZE);
	if (!_file.good()) {
		std::cerr << "Failed to read entire block" << std::endl;
		exit(-1);
	}
}


// Writes the data in block to disk block block_num.
void Disk::write_block(int block_num, void *block)
{
	off_t offset;

	if (block_num < 0 || block_num >= NUM_BLOCKS) {
		std::cerr << "Invalid block size" << std::endl;
		exit(-1);
	}

	offset = block_num * BLOCK_SIZE;
	_file.seekp(offset);
	if (!_file.good()) {
		std::cerr << "Seek failure" << std::endl;
		exit(-1);
	}

	_file.write(reinterpret_cast<char*>(block), BLOCK_SIZE);
	_file.flush();
	if (!_file.good()) {
		std::cerr << "Failed to read entire block" << std::endl;
		exit(-1);
	}
}
