#include <cerrno>  // errno
#include <cstdlib>  // atoi
#include <cstring>  // memset, strerror
#include <functional>  // function
#include <iostream>  // cout, cerr
#include <string>  // string, stoi
#include <type_traits>  // underlying_type
#include <unordered_map>  // unordered_map
#include <utility>  // make_pair

#include "FileSys.h"


#if _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>


namespace
{
	using ssize_t = std::intmax_t;
	using socket_t = SOCKET;


	int close(SOCKET a_sock)
	{
		return closesocket(a_sock);
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
#include <netinet/in.h>
#include <netdb.h>

#ifndef
#define INVALID_SOCKET -1;
#endif


namespace
{
	using socket_t = int;
}
#endif


#undef DispatchMessage


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
}


class CommandParser
{
public:
	CommandParser() = delete;


	explicit CommandParser(socket_t a_sock)
	{
		_fs.mount(a_sock);

		_commandTable.insert(std::make_pair("mkdir", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string directory(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.mkdir(directory.c_str());
		}));

		_commandTable.insert(std::make_pair("ls", [this](const std::string& a_msg) -> void
		{
			_fs.ls();
		}));

		_commandTable.insert(std::make_pair("cd", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string directory(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.cd(directory.c_str());
		}));

		_commandTable.insert(std::make_pair("home", [this](const std::string& a_msg) -> void
		{
			_fs.home();
		}));

		_commandTable.insert(std::make_pair("rmdir", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string directory(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.rmdir(directory.c_str());
		}));

		_commandTable.insert(std::make_pair("create", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string fileName(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.create(fileName.c_str());
		}));

		_commandTable.insert(std::make_pair("append", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos1 = a_msg.find_first_of(' ') + 1;
			std::string::size_type pos2 = a_msg.find_first_of(' ', pos1);
			std::string fileName(a_msg, pos1, pos2++ - pos1);
			std::string data(a_msg, pos2, a_msg.find_first_of('\r', pos2) - pos2);
			_fs.append(fileName.c_str(), data.c_str());
		}));

		_commandTable.insert(std::make_pair("stat", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string name(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.stat(name.c_str());
		}));

		_commandTable.insert(std::make_pair("cat", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string fileName(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.cat(fileName.c_str());
		}));

		_commandTable.insert(std::make_pair("head", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos1 = a_msg.find_first_of(' ') + 1;
			std::string::size_type pos2 = a_msg.find_first_of(' ', pos1);
			std::string fileName(a_msg, pos1, pos2++ - pos1);
			std::string size(a_msg, pos2, a_msg.find_first_of('\r', pos2) - pos2);
			_fs.head(fileName.c_str(), std::stoi(size));
		}));

		_commandTable.insert(std::make_pair("rm", [this](const std::string& a_msg) -> void
		{
			std::string::size_type pos = a_msg.find_first_of(' ') + 1;
			std::string fileName(a_msg, pos, a_msg.find_first_of('\r') - pos);
			_fs.rm(fileName.c_str());
		}));
	}


	// calls the corresponding command in the passed message
	bool operator()(const std::string& a_msg)
	{
		std::string key(a_msg, 0, a_msg.find_first_of(" \r"));
		auto result = _commandTable.find(key);
		if (result != _commandTable.end()) {
			result->second(a_msg);
			return true;
		} else {
			return false;
		}
	}


	// retrieves the last response from the filesystem
	std::string getQueryResponse() const
	{
		return _fs.getQueryResponse();
	}


	// retrieves the last error from the filesystem
	FileError getLastErr() const noexcept
	{
		return _fs.getLastErr();
	}


	~CommandParser()
	{
		_fs.unmount();
	}

private:
	using Command = void(const std::string& a_msg);
	using CommandTable = std::unordered_map<std::string, std::function<Command>>;


	FileSys _fs;
	CommandTable _commandTable;
};


struct Response
{
public:
	Response() = default;
	explicit Response(const std::string& a_msg, bool a_good) :
		msg(a_msg),
		good(a_good)
	{}


	explicit operator bool()
	{
		return good;
	}


	std::string msg;	// msg recieved from response
	bool good;	// true == no errors while getting response
};


// Handles response message from socket connection
Response HandleResponse(socket_t a_sock)
{
	static char buf[8000];
	std::size_t pos = 0;
	ssize_t msgSize = 0;
	ssize_t result = 1;
	while (result != 0) {
		result = read(a_sock, buf + msgSize, sizeof(buf) - msgSize);
		if (result == -1) {
			std::cerr << "Read failed with error \"" << std::strerror(errno) << "\"" << std::endl;
			return Response("", false);
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
	return Response(std::string(buf, msgSize), true);
}


// Prepares the message for dispatch to socket connection
std::string PrepareMessage(FileError a_lastErr, std::string a_extraMessage)
{
	std::string header1(std::to_string(static_cast<std::underlying_type<decltype(a_lastErr)>::type>(a_lastErr)));
	std::string header2 = "Length: " + std::to_string(a_extraMessage.length()) + "\r\n";
	std::string empty("\r\n");

	switch (a_lastErr) {
	case FileError::kFileNotDir:
		header1 += " FILE_NOT_DIR";
		break;
	case FileError::kFileIsDir:
		header1 += " FILE_IS_DIR";
		break;
	case FileError::kFileExists:
		header1 += " FILE_EXISTS";
		break;
	case FileError::kFileNotExists:
		header1 += " FILE_NOT_EXISTS";
		break;
	case FileError::kFileNameTooLong:
		header1 += " FILE_NAME_TOO_LONG";
		break;
	case FileError::kDiskFull:
		header1 += " DISK_FULL";
		break;
	case FileError::kDirFull:
		header1 += " DIR_FULL";
		break;
	case FileError::kDirNotEmpty:
		header1 += " DIR_NOT_EMPTY";
		break;
	case FileError::kAppendExceedsMaxSize:
		header1 += " APPEND_EXCEEDS_MAX_SIZE";
		break;
	case FileError::kCommandNotFound:
		header1 += " COMMAND_NOT_FOUND";
		break;
	case FileError::kOK:
	default:
		header1 += " OK";
		break;
	}
	header1 += "\r\n";

	return header1 + header2 + empty + a_extraMessage;
}


// Dispatches the message to socket listener
bool DispatchMessage(socket_t a_sock, const std::string& a_msg)
{
	ssize_t i = 0;
	while (i < a_msg.length() + 1) {
		ssize_t result = write(a_sock, a_msg.data() + i, a_msg.length() - i + 1);
		if (result == -1) {
			std::cerr << "Write failed with error \"" << std::strerror(errno) << "\"" << std::endl;
			return false;
		} else {
			i += result;
		}
	}
	return true;
}


int main(int argc, char* argv[])
{
	unsigned short port;
	if (argc != 2) {
		std::cout << "Usage: ./nfsserver port#\n";
		return -1;
	} else {
		port = std::atoi(argv[1]);
	}

#if _WIN32
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		std::cerr << "WSAStartup failed with error \"" << std::strerror(errno) << "\"" << std::endl;
		return -1;
	}
#endif

	sockaddr_in serverAddr;
	std::memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(port);

	// create
	socket_t listenSock = socket(serverAddr.sin_family, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock == INVALID_SOCKET) {
		std::cerr << "Socket creation failed with error \"" << std::strerror(errno) << "\"" << std::endl;
#if _WIN32
		WSACleanup();
#endif
		return -1;
	}

	// bind
	if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) != 0) {
		std::cerr << "Socket binding failed with error \"" << std::strerror(errno) << "\"" << std::endl;
		close(listenSock);
#if _WIN32
		WSACleanup();
#endif
		return -1;
	}

	// listen
	if (listen(listenSock, 0) != 0) {
		std::cerr << "Socket listen failed with error \"" << std::strerror(errno) << "\"" << std::endl;
		close(listenSock);
#if _WIN32
		WSACleanup();
#endif
		return -1;
	} else {
		std::cout << "Waiting for connection..." << dendl;
	}

	// accept
	socket_t acceptSock = accept(listenSock, 0, 0);
	if (acceptSock == INVALID_SOCKET) {
		std::cerr << "Socket accept failed with error \"" << std::strerror(errno) << "\"" << std::endl;
		close(listenSock);
#if _WIN32
		WSACleanup();
#endif
		return -1;
	} else {
		std::cout << "Client connected" << dendl;
	}

	// communication
	CommandParser parser(acceptSock);
	Response response;
	while (response = HandleResponse(acceptSock)) {
		std::string msg;
		if (!parser(response.msg)) {
			msg = PrepareMessage(FileError::kCommandNotFound, "");
		} else {
			msg = PrepareMessage(parser.getLastErr(), parser.getQueryResponse());
		}
		if (!DispatchMessage(acceptSock, msg)) {
			break;
		}
	}

	// cleanup
	close(listenSock);
#if _WIN32
	WSACleanup();
#endif
	return 0;
}
