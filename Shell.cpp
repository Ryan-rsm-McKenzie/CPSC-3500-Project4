// CPSC 3500: Shell
// Implements a basic shell (command line interface) for the file system

#include "Shell.h"

#include <cerrno>  // errno
#include <cstdint>  // intmax_t
#include <cstdlib>  // size_t
#include <cstring>  // strerror, memset
#include <fstream>  // ifstream
#include <iostream>  // cerr, endl, cout, cin
#include <sstream>  // stringstream
#include <stdexcept>  // out_of_range
#include <string>  // string, getline, to_string, stoi
#include <vector>  // vector


#if _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>


namespace
{
	int close(SOCKET a_sock)
	{
		return closesocket(a_sock);
		WSACleanup();
	}


	ssize_t write(SOCKET a_sock, const char* a_buf, std::size_t a_count)
	{
		return send(a_sock, a_buf, a_count, 0);
	}


	ssize_t read(SOCKET a_sock, char* a_buf, std::size_t a_count)
	{
		return recv(a_sock, a_buf, a_count, 0);
	}
}
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif


namespace
{
	// endl that flushes only in debug
	template<class CharT, class Traits>
	std::basic_ostream<CharT, Traits>& dendl(std::basic_ostream<CharT, Traits>& a_os)
	{
		a_os.put(a_os.widen('\n'));
#if _DEBUG
		a_os.flush();
#endif
		return a_os;
	}


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


	constexpr char PROMPT_STRING[] = "NFS> ";	// shell prompt
}


// Mount the network file system with server name and port number in the format of server:port
void Shell::mountNFS(std::string a_fsLoc)
{
	std::string server;
	std::string port;
	try {
		std::string::size_type pos = a_fsLoc.find_first_of(':');
		server = a_fsLoc.substr(0, pos);
		port = a_fsLoc.substr(pos + 1);
	} catch (std::out_of_range& e) {
		std::cerr << e.what() << std::endl;
		return;
	}

#if _WIN32
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		std::cerr << "WSAStartup failed with error \"" << std::strerror(errno) << "\"" << std::endl;
		return;
	}
#endif

	addrinfo* result = 0;
	addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	int errCode = getaddrinfo(server.c_str(), port.c_str(), &hints, &result);
	if (errCode != 0) {
		std::cerr << "Failed to get address info with error \"" << gai_strerror(errCode) << "\"" << std::endl;
	} else {
		for (addrinfo* ptr = result; !_isMounted && ptr; ptr = ptr->ai_next) {
			_csSock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
			if (_csSock == INVALID_SOCKET) {
				std::cerr << "Socket creation failed with error \"" << std::strerror(errno) << "\"" << std::endl;
			} else if (connect(_csSock, ptr->ai_addr, ptr->ai_addrlen) != 0) {
				std::cerr << "Connection failed with error \"" << std::strerror(errno) << "\"" << std::endl;
				if (close(_csSock) != 0) {
					std::cerr << "Socket close failed with error \"" << std::strerror(errno) << "\"" << std::endl;
				}
			} else {
				_isMounted = true;
			}
		}
		freeaddrinfo(result);
	}

	if (!_isMounted) {
#if _WIN32
		WSACleanup();
#endif
		_csSock = INVALID_SOCKET;
	}
}


// Unmount the network file system if it was mounted
void Shell::unmountNFS()
{
	if (!_isMounted) {
		return;
	} else {
		close(_csSock);
		_csSock = INVALID_SOCKET;
		_isMounted = false;
	}
}


// Remote procedure call on mkdir
void Shell::mkdir_rpc(std::string a_dirName)
{
	std::string msg = "mkdir " + a_dirName + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on cd
void Shell::cd_rpc(std::string a_dirName)
{
	std::string msg = "cd " + a_dirName + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on home
void Shell::home_rpc()
{
	std::string msg = "home\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on rmdir
void Shell::rmdir_rpc(std::string a_dirName)
{
	std::string msg = "rmdir " + a_dirName + "\r\n";
	SendMessageAndHandleResponse(msg);
}

// Remote procedure call on ls
void Shell::ls_rpc()
{
	std::string msg = "ls\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on create
void Shell::create_rpc(std::string a_fileNname)
{
	std::string msg = "create " + a_fileNname + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on append
void Shell::append_rpc(std::string a_fileNname, std::string a_data)
{
	std::string msg = "append " + a_fileNname + " " + a_data + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procesure call on cat
void Shell::cat_rpc(std::string a_fileNname)
{
	std::string msg = "cat " + a_fileNname + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on head
void Shell::head_rpc(std::string a_fileNname, int a_size)
{
	std::string msg = "head " + a_fileNname + " " + std::to_string(a_size) + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on rm
void Shell::rm_rpc(std::string a_fileNname)
{
	std::string msg = "rm " + a_fileNname + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Remote procedure call on stat
void Shell::stat_rpc(std::string a_fileNname)
{
	std::string msg = "stat " + a_fileNname + "\r\n";
	SendMessageAndHandleResponse(msg);
}


// Executes the shell until the user quits.
void Shell::run()
{
	// make sure that the file system is mounted
	if (!_isMounted)
		return;

	// continue until the user quits
	bool user_quit = false;
	while (!user_quit) {
		// print prompt and get command line
		std::string command_str;
		std::cout << PROMPT_STRING;
		std::getline(std::cin, command_str);

		// execute the command
		user_quit = execute_command(command_str);
	}

	// unmount the file system
	unmountNFS();
}


// Execute a script.
void Shell::run_script(char *file_name)
{
	// make sure that the file system is mounted
	if (!_isMounted)
		return;
	// open script file
	std::ifstream infile;
	infile.open(file_name);
	if (infile.fail()) {
		std::cerr << "Could not open script file" << std::endl;
		return;
	}


	// execute each line in the script
	bool user_quit = false;
	std::string command_str;
	getline(infile, command_str, '\n');
	while (!infile.eof() && !user_quit) {
		std::cout << PROMPT_STRING << command_str << std::endl;
		user_quit = execute_command(command_str);
		getline(infile, command_str);
	}

	// clean up
	unmountNFS();
	infile.close();
}


// Executes the command. Returns true for quit and false otherwise.
bool Shell::execute_command(std::string command_str)
{
	// parse the command line
	struct Command command = parse_command(command_str);

	// look for the matching command
	if (command.name == "") {
		return false;
	} else if (command.name == "mkdir") {
		mkdir_rpc(command.file_name);
	} else if (command.name == "cd") {
		cd_rpc(command.file_name);
	} else if (command.name == "home") {
		home_rpc();
	} else if (command.name == "rmdir") {
		rmdir_rpc(command.file_name);
	} else if (command.name == "ls") {
		ls_rpc();
	} else if (command.name == "create") {
		create_rpc(command.file_name);
	} else if (command.name == "append") {
		append_rpc(command.file_name, command.append_data);
	} else if (command.name == "cat") {
		cat_rpc(command.file_name);
	} else if (command.name == "head") {
		errno = 0;
		unsigned long n = strtoul(command.append_data.c_str(), NULL, 0);
		if (0 == errno) {
			head_rpc(command.file_name, n);
		} else {
			std::cerr << "Invalid command line: " << command.append_data;
			std::cerr << " is not a valid number of bytes" << std::endl;
			return false;
		}
	} else if (command.name == "rm") {
		rm_rpc(command.file_name);
	} else if (command.name == "stat") {
		stat_rpc(command.file_name);
	} else if (command.name == "quit") {
		return true;
	}

	return false;
}


// Parses a command line into a command struct. Returned name is blank
// for invalid command lines.
Shell::Command Shell::parse_command(std::string command_str)
{
	// empty command struct returned for errors
	struct Command empty = { "", "", "" };

	// grab each of the tokens (if they exist)
	struct Command command;
	std::stringstream ss(command_str);
	int num_tokens = 0;
	if (ss >> command.name) {
		num_tokens++;
		if (ss >> command.file_name) {
			num_tokens++;
			if (ss >> command.append_data) {
				num_tokens++;
				std::string junk;
				if (ss >> junk) {
					num_tokens++;
				}
			}
		}
	}

	// Check for empty command line
	if (num_tokens == 0) {
		return empty;
	}

	// Check for invalid command lines
	if (command.name == "ls" ||
		command.name == "home" ||
		command.name == "quit") {
		if (num_tokens != 1) {
			std::cerr << "Invalid command line: " << command.name;
			std::cerr << " has improper number of arguments" << std::endl;
			return empty;
		}
	} else if (command.name == "mkdir" ||
		command.name == "cd" ||
		command.name == "rmdir" ||
		command.name == "create" ||
		command.name == "cat" ||
		command.name == "rm" ||
		command.name == "stat") {
		if (num_tokens != 2) {
			std::cerr << "Invalid command line: " << command.name;
			std::cerr << " has improper number of arguments" << std::endl;
			return empty;
		}
	} else if (command.name == "append" || command.name == "head") {
		if (num_tokens != 3) {
			std::cerr << "Invalid command line: " << command.name;
			std::cerr << " has improper number of arguments" << std::endl;
			return empty;
		}
	} else {
		std::cerr << "Invalid command line: " << command.name;
		std::cerr << " is not a command" << std::endl;
		return empty;
	}

	return command;
}


void Shell::SendMessageAndHandleResponse(const std::string& a_message)
{
	if (!SendMessage(a_message) || !HandleResponse()) {
		unmountNFS();
	}
}


bool Shell::SendMessage(const std::string& a_message)
{
	ssize_t i = 0;
	while (i < a_message.length() + 1) {
		ssize_t result = write(_csSock, a_message.data() + i, a_message.length() - i + 1);
		if (result == -1) {
			std::cerr << "Write failed with error \"" << std::strerror(errno) << "\"" << std::endl;
			return false;
		} else {
			i += result;
		}
	}
	return true;
}


bool Shell::HandleResponse()
{
	static char buf[8000];
	std::size_t pos = 0;
	ssize_t msgSize = 0;
	ssize_t result = 1;
	while (result != 0) {
		result = read(_csSock, buf + msgSize, sizeof(buf) - msgSize);
		if (result == -1) {
			std::cerr << "Read failed with error \"" << std::strerror(errno) << "\"" << std::endl;
			return false;
		} else {
			msgSize += result;
			while (pos < msgSize) {
				if (buf[pos] == '\0') {
					result = 0;
					break;
				}
				++pos;
			}
		}
	}

	PrintResponse(buf, msgSize);
	return true;
}


void Shell::PrintResponse(const char* buf, ssize_t a_bufLen)
{
	try {
		std::string msg(buf, a_bufLen);
		std::string::size_type pos1 = msg.find_first_of('\n') + 1;
		std::string header1(msg, 0, pos1);
		std::string::size_type pos2 = msg.find_first_of('\n', pos1) + 1;
		std::string header2(msg, pos1, pos2 - pos1);
		std::string::size_type pos3 = msg.find_first_of('\n', pos2) + 1;

		FileError statusCode = static_cast<FileError>(std::stoi(header1));
		switch (statusCode) {
		case FileError::kFileNotDir:
			std::cerr << "File is not a directory!" << std::endl;
			break;
		case FileError::kFileIsDir:
			std::cerr << "File is a directory!" << std::endl;
			break;
		case FileError::kFileExists:
			std::cerr << "File exists!" << std::endl;
			break;
		case FileError::kFileNotExists:
			std::cerr << "File does not exist!" << std::endl;
			break;
		case FileError::kFileNameTooLong:
			std::cerr << "File name is too long!" << std::endl;
			break;
		case FileError::kDiskFull:
			std::cerr << "Disk is full!" << std::endl;
			break;
		case FileError::kDirFull:
			std::cerr << "Directory is full!" << std::endl;
			break;
		case FileError::kDirNotEmpty:
			std::cerr << "Directory is not empty!" << std::endl;
			break;
		case FileError::kAppendExceedsMaxSize:
			std::cerr << "Append exceeds maximum filesize!" << std::endl;
			break;
		case FileError::kCommandNotFound:
			std::cerr << "Command not found!" << std::endl;
			break;
		default:
			break;
		}

		std::size_t extraLen = std::stoull(header2.substr(header2.find_first_of(' ')));
		if (extraLen > 0) {
			std::string extraMsg(msg, pos3, extraLen);
			std::cout << extraMsg;
		}
		std::cout << dendl;
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}
