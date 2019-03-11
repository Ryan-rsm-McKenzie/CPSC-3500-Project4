// CPSC 3500: File System
// Implements the file system commands that are available to the shell.

#include "FileSys.h"

#include <cstdlib>  // size_t
#include <cstring>  // strlen, strcmp, strcpy, memset
#include <iostream>  // cerr, endl
#include <ostream>  // basic_ostream
#include <stdexcept>  // runtime_error
#include <string>  // to_string
#include <utility>  // make_pair
#include <vector>  // vector

#ifdef _WIN32
#include <WinSock2.h>  // SOCKET, closesocket


namespace
{
	int close(socket_t a_sock)
	{
		return closesocket(a_sock);
	}
}
#else
#include <unistd.h>
#endif

#include "BasicFileSys.h"
#include "Blocks.h"


class bad_block_alloc : public std::runtime_error
{
public:
	explicit bad_block_alloc(const char* a_fileName) :
		std::runtime_error("")
	{
		_what = "Disk is full when attempting to append data to file with name \"";
		_what += a_fileName;
		_what += "\"!";
	}


	virtual const char* what() const noexcept override
	{
		return _what.c_str();
	}

private:
	std::string _what;
};


FileSys::FileSys() :
	_curDirHandle(kInvalidHandle),
	_fsSock(INVALID_SOCKET),
	_lastErr(FileError::kOK),
	_response("")
{}


// mounts the file system
void FileSys::mount(int a_sock)
{
	_bfs.mount();
	_curDirHandle = kRootDirHandle; //by default current directory is home directory, in disk block #1
	_fsSock = a_sock; //use this socket to receive file system operations from the client and send back response messages
}


// unmounts the file system
void FileSys::unmount()
{
	_bfs.unmount();
	close(_fsSock);
}


// make a directory
void FileSys::mkdir(const char* a_name)
{
	MakeBlock<dirblock_t>(a_name);
}


// switch to a directory
void FileSys::cd(const char* a_name)
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		_curDirHandle = entry->block_num;
	} else {
		PrintFailedToFindFile(a_name);
	}
}


// switch to home directory
void FileSys::home()
{
	_curDirHandle = kRootDirHandle;
}


// remove a directory
void FileSys::rmdir(const char* a_name)
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		auto rmDir = ReadDirBlock(entry->block_num);
		if (!rmDir.second) {
			std::cerr << "File with name \"" << a_name << "\" is not a directory!" << std::endl;
			_lastErr = FileError::kFileNotDir;
			return;
		}

		if (rmDir.first.num_entries == 0) {
			_bfs.reclaim_block(entry->block_num);
			entry->block_num = kInvalidHandle;
			--curDir.first.num_entries;
			_bfs.write_block(_curDirHandle, &curDir.first);
		} else {
			std::cerr << "Directory with name \"" << a_name << "\" is not empty!" << std::endl;
			_lastErr = FileError::kDirNotEmpty;
		}
	} else {
		PrintFailedToFindFile(a_name);
	}
}


// list the contents of current directory
void FileSys::ls()
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	ForEachDirEntry(curDir.first, [this](DirEntry& a_entry) -> bool
	{
		static char buf[BLOCK_SIZE];
		if (a_entry.block_num != kInvalidHandle) {
			_response << a_entry.name;
			_bfs.read_block(a_entry.block_num, &buf);
			if (IsDirectory(buf)) {
				_response << '/';
			}
			_response << '\n';
		}
		return false;
	});
	_response << '\n';
}


// create an empty data file
void FileSys::create(const char* a_name)
{
	MakeBlock<inode_t>(a_name);
}


// append data to a data file
void FileSys::append(const char* a_name, const char* a_data)
{
	if (*a_data == '\0') {
		return;
	}

	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		auto iNode = ReadINodeBlock(entry->block_num);
		if (!iNode.second) {
			return;
		}

		std::size_t dataLen = std::strlen(a_data);
		if (dataLen > MAX_FILE_SIZE - iNode.first.size) {
			std::cerr << "Buffer overflow when attempting to write data to file with name \"" << a_name << "\"!" << std::endl;
			_lastErr = FileError::kAppendExceedsMaxSize;
			return;
		}

		// allocate new blocks
		std::vector<BlockHandle> handles;
		std::size_t freeCount = BLOCK_SIZE - iNode.first.size % BLOCK_SIZE;
		std::size_t allocSize = dataLen > freeCount ? dataLen - freeCount : 0;
		try {
			std::size_t numAllocBlocks = allocSize / BLOCK_SIZE;	// full blocks
			if (allocSize % BLOCK_SIZE != 0) {	// partial fill block
				++numAllocBlocks;
			}
			if (iNode.first.blocks[iNode.first.size / BLOCK_SIZE] == kInvalidHandle) {	// current block
				++numAllocBlocks;
			}

			while (numAllocBlocks--) {
				handles.push_back(_bfs.get_free_block());
				if (handles.back() == kInvalidHandle) {
					throw bad_block_alloc(a_name);
				}
			}
		} catch (bad_block_alloc& e) {
			std::cerr << e.what() << std::endl;
			_lastErr = FileError::kDiskFull;
			for (auto& handle : handles) {
				if (handle != kInvalidHandle) {
					_bfs.reclaim_block(handle);
				}
			}
			return;
		}

		// assign block handles
		for (std::size_t i = iNode.first.size / BLOCK_SIZE; i < MAX_DATA_BLOCKS && !handles.empty(); ++i) {
			if (iNode.first.blocks[i] == kInvalidHandle) {
				iNode.first.blocks[i] = handles.back();
				handles.pop_back();
			}
		}

		// copy data
		std::size_t dataIdx = 0;
		while (dataIdx < dataLen) {
			BlockHandle dataHandle = iNode.first.blocks[iNode.first.size / BLOCK_SIZE];
			datablock_t dataBlock;
			_bfs.read_block(dataHandle, &dataBlock);
			for (int blockIdx = iNode.first.size % BLOCK_SIZE; blockIdx < BLOCK_SIZE && dataIdx < dataLen; ++blockIdx) {
				dataBlock.data[blockIdx] = a_data[dataIdx++];
				++iNode.first.size;
			}
			_bfs.write_block(dataHandle, &dataBlock);
		}
		_bfs.write_block(entry->block_num, &iNode.first);
	} else {
		PrintFailedToFindFile(a_name);
	}
}


// display the contents of a data file
void FileSys::cat(const char* a_name)
{
	head(a_name, MAX_FILE_SIZE);
}


// display the first N bytes of the file
void FileSys::head(const char* a_name, unsigned int a_size)
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		auto iNode = ReadINodeBlock(entry->block_num);
		if (!iNode.second) {
			return;
		}

		if (iNode.first.size == 0) {
			return;
		}

		std::size_t size = a_size < iNode.first.size ? a_size : iNode.first.size;
		std::size_t numBlocks = size / BLOCK_SIZE + 1;
		for (std::size_t i = 0; i < numBlocks; ++i) {
			datablock_t dataBlock;
			_bfs.read_block(iNode.first.blocks[i], &dataBlock);
			if (i == numBlocks - 1) {
				_response.write(dataBlock.data, size % BLOCK_SIZE);
			} else {
				_response.write(dataBlock.data, BLOCK_SIZE);
			}
		}
		_response << '\n';
	} else {
		PrintFailedToFindFile(a_name);
	}
}


// delete a data file
void FileSys::rm(const char* a_name)
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		auto iNode = ReadINodeBlock(entry->block_num);
		if (!iNode.second) {
			return;
		}

		if (iNode.first.size != 0) {
			std::size_t numBlocks = iNode.first.size / MAX_DATA_BLOCKS + 1;
			for (std::size_t i = 0; i < numBlocks; ++i) {
				_bfs.reclaim_block(iNode.first.blocks[i]);
			}
		}
		_bfs.reclaim_block(entry->block_num);
		entry->block_num = kInvalidHandle;
		--curDir.first.num_entries;
		_bfs.write_block(_curDirHandle, &curDir.first);
	} else {
		PrintFailedToFindFile(a_name);
	}
}


// display stats about file or directory
void FileSys::stat(const char* a_name)
{
	auto curDir = ReadDirBlock(_curDirHandle);
	if (!curDir.second) {
		return;
	}

	DirEntry* entry = ForEachDirEntry(curDir.first, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		char buf[BLOCK_SIZE];
		_bfs.read_block(entry->block_num, buf);
		if (IsDirectory(buf)) {
			_response << "Directory name: " << entry->name << '/' << '\n';
			_response << "Directory block: " << entry->block_num << '\n';
		} else {
			inode_t* iNode = reinterpret_cast<inode_t*>(buf);
			_response << "iNode block: " << entry->block_num << '\n';
			_response << "Bytes in files: " << iNode->size << '\n';
			_response << "Number of blocks: " << (iNode->size == 0 ? 1 : iNode->size / BLOCK_SIZE + 2) << '\n';
			_response << "First block: " << (iNode->size == 0 ? "N/A" : std::to_string(iNode->blocks[0])) << '\n';
		}
	} else {
		PrintFailedToFindFile(a_name);
	}
}


std::string FileSys::getQueryResponse() const
{
	std::string tmp = _response.str();
	while (!tmp.empty() && tmp.back() == '\n') {
		tmp.pop_back();
	}
	tmp.push_back('\n');
	_response.str("");
	return tmp;
}


FileError FileSys::getLastErr() const noexcept
{
	auto tmp = _lastErr;
	_lastErr = FileError::kOK;
	return tmp;
}


bool FileSys::IsDirectory(void* a_block) const
{
	return *reinterpret_cast<decltype(DIR_MAGIC_NUM)*>(a_block) == DIR_MAGIC_NUM;
}


bool FileSys::IsINode(void* a_block) const
{
	return *reinterpret_cast<decltype(INODE_MAGIC_NUM)*>(a_block) == INODE_MAGIC_NUM;
}


void FileSys::InitializeBlock(dirblock_t& a_block) const
{
	std::memset(&a_block, 0, sizeof(decltype(a_block)));
	a_block.magic = DIR_MAGIC_NUM;
}


void FileSys::InitializeBlock(inode_t& a_block) const
{
	std::memset(&a_block, 0, sizeof(decltype(a_block)));
	a_block.magic = INODE_MAGIC_NUM;
}


void FileSys::InitializeBlock(datablock_t& a_block) const
{
	std::memset(&a_block, 0, sizeof(decltype(a_block)));
}


bool FileSys::InsertIntoDirectory(dirblock_t& a_dir, BlockHandle a_handle, const char* a_name)
{
	DirEntry* entry = ForEachDirEntry(a_dir, [a_name](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num != kInvalidHandle && std::strcmp(a_entry.name, a_name) == 0;
	});

	if (entry) {
		std::cerr << "File with name \"" << a_name << "\" already exists!" << std::endl;
		_lastErr = FileError::kFileExists;
		return false;
	}

	if (a_dir.num_entries >= MAX_DIR_ENTRIES) {
		std::cerr << "Encountered directory overflow when writing directory with name \"" << a_name << "\"!" << std::endl;
		_lastErr = FileError::kDirFull;
		return false;
	}
	if (std::strlen(a_name) > MAX_FNAME_SIZE) {
		std::cerr << "Encountered buffer overflow when attempting to write directory with name \"" << a_name << "\"!" << std::endl;
		_lastErr = FileError::kFileNameTooLong;
		return false;
	}

	entry = ForEachDirEntry(a_dir, [](DirEntry& a_entry) -> bool
	{
		return a_entry.block_num == kInvalidHandle;
	});

	if (entry) {
		std::strcpy(entry->name, a_name);
		entry->block_num = a_handle;
		++a_dir.num_entries;
		return true;
	} else {
		std::cerr << "Could not find free block in directory! Mismatch on declared and actual entries!" << std::endl;	// This indicates file corruption
		return false;
	}
}


std::pair<dirblock_t, bool> FileSys::ReadDirBlock(BlockHandle a_handle)
{
	dirblock_t block;
	_bfs.read_block(a_handle, &block);
	bool second = IsDirectory(&block);
	if (!second) {
		std::cerr << "Block number " << a_handle << " is not a directory!" << std::endl;
		_lastErr = FileError::kFileNotDir;
	}
	return std::make_pair(block, second);
}


std::pair<inode_t, bool> FileSys::ReadINodeBlock(BlockHandle a_handle)
{
	inode_t block;
	_bfs.read_block(a_handle, &block);
	bool second = IsINode(&block);
	if (!second) {
		std::cerr << "Block number " << a_handle << " is not an iNode!" << std::endl;
		_lastErr = FileError::kFileIsDir;
	}
	return std::make_pair(block, second);
}


void FileSys::PrintFailedToFindFile(const char* a_fileName) const
{
	std::cerr << "Failed to find file with name \"" << a_fileName << "\"!" << std::endl;
	_lastErr = FileError::kFileNotExists;
}
