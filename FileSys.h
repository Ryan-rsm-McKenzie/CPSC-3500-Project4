// CPSC 3500: File System
// Implements the file system commands that are available to the shell.

#ifndef FILESYS_H
#define FILESYS_H


#include <iostream>  // cerr
#include <sstream>  // stringstream
#include <type_traits>  // remove_reference
#include <utility>  // pair

#include "BasicFileSys.h"
#include "Blocks.h"


#if _WIN32
#include <WinSock2.h>


namespace
{
	using socket_t = SOCKET;
}
#else
namespace
{
	using socket_t = int;
}

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#endif


enum class FileError
{
	kOK = 0,
	kFileNotDir = 500,	// cd, rmdir
	kFileIsDir,	// cat, head, append, rm
	kFileExists,	// create, mkdir
	kFileNotExists,	// cd, rmdir, cat, head, append, rm, stat
	kFileNameTooLong,	// create, mkdir
	kDiskFull,	// create, mkdir, append
	kDirFull,	// create, mkdir
	kDirNotEmpty,	// rmdir
	kAppendExceedsMaxSize,	// append
	kCommandNotFound
};


class FileSys
{
public:
	FileSys();

	// mounts the file system
	void mount(int a_sock);

	// unmounts the file system
	void unmount();

	// make a directory
	void mkdir(const char* a_name);

	// switch to a directory
	void cd(const char* a_name);

	// switch to home directory
	void home();

	// remove a directory
	void rmdir(const char* a_name);

	// list the contents of current directory
	void ls();

	// create an empty data file
	void create(const char* a_name);

	// append data to a data file
	void append(const char* a_name, const char* a_data);

	// display the contents of a data file
	void cat(const char* a_name);

	// display the first N bytes of the file
	void head(const char* a_name, unsigned int a_size);

	// delete a data file
	void rm(const char* a_name);

	// display stats about file or directory
	void stat(const char* a_name);

	std::string getQueryResponse() const;	// returns and clears the response message from the last issued command
	FileError getLastErr() const noexcept;	// returns and clears the last encountered error

private:
	using BlockHandle = decltype(dirblock_t::dir_entries[0].block_num);	// type for block handle
	using DirEntry = std::remove_reference<decltype(dirblock_t::dir_entries[0])>::type;	// type for directory entry


	enum
	{
		kInvalidHandle = 0,
		kSuperBlockHandle = 0,
		kRootDirHandle = 1
	};


	bool IsDirectory(void* a_block) const;	// returns true if the block is a directory
	bool IsINode(void* a_block) const;	// returns true if the block is an inode
	void InitializeBlock(dirblock_t& a_block) const;	// initializes the directory block
	void InitializeBlock(inode_t& a_block) const;	// initializes the iNode block
	void InitializeBlock(datablock_t& a_block) const;	// initializes the data block
	bool InsertIntoDirectory(dirblock_t& a_dir, BlockHandle a_handle, const char* a_name);	// inserts the block into the directory
	std::pair<dirblock_t, bool> ReadDirBlock(BlockHandle a_handle);	// first == directory block, second == success/failure
	std::pair<inode_t, bool> ReadINodeBlock(BlockHandle a_handle);	// first == iNode block, second == success/failure
	void PrintFailedToFindFile(const char* a_fileName) const;	// prints an error message indicating failure to find the specified file
	template <typename Condition> DirEntry* ForEachDirEntry(dirblock_t& a_directory, Condition a_func);	// iterates over each entry in the directory, uses a_func to determine when to stop
	template <typename BlockType> void MakeBlock(const char* a_name);	// Makes a block of the given type


	// members
	BasicFileSys _bfs;	// basic file system
	BlockHandle _curDirHandle;	// current directory
	socket_t _fsSock;  // file server socket
	mutable FileError _lastErr;	// last encountered error
	mutable std::stringstream _response;	// response message to last command
};


// using Condition = bool(DirEntry& a_entry);
template <typename Condition>
auto FileSys::ForEachDirEntry(dirblock_t& a_directory, Condition a_func)
->DirEntry*
{
	for (std::size_t i = 0; i < MAX_DIR_ENTRIES; ++i) {
		if (a_func(a_directory.dir_entries[i])) {
			return &a_directory.dir_entries[i];
		}
	}
	return 0;
}


template <typename BlockType>
void FileSys::MakeBlock(const char* a_name)
{
	dirblock_t curDir;
	_bfs.read_block(_curDirHandle, &curDir);

	BlockHandle handle = _bfs.get_free_block();
	if (handle == kInvalidHandle) {
		std::cerr << "Disk is full when creating file with name \"" << a_name << "\"" << std::endl;
		_lastErr = FileError::kDiskFull;
		return;
	}
	BlockType block;
	InitializeBlock(block);

	if (!InsertIntoDirectory(curDir, handle, a_name)) {
		_bfs.reclaim_block(handle);
	} else {
		_bfs.write_block(handle, &block);
		_bfs.write_block(_curDirHandle, &curDir);
	}
}


#endif
