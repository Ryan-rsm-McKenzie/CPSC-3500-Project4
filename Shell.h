// CPSC 3500: Shell
// Implements a basic shell (command line interface) for the file system

#ifndef SHELL_H
#define SHELL_H


#include <cstdint>  // intmax_t
#include <string>  // string


#if _WIN32
#include <WinSock2.h>


namespace
{
	using ssize_t = std::intmax_t;
	using socket_t = SOCKET;
}
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1;
#endif


namespace
{
	using socket_t = int;
}
#endif


#undef SendMessage


// Shell
class Shell
{
public:
	//constructor, do not change it!!
	Shell() :
		_csSock(INVALID_SOCKET),
		_isMounted(false)
	{}

	// Mount a network file system located in host:port, set is_mounted = true if success
	void mountNFS(std::string fs_loc);  //fs_loc must be in the format of server:port

	//unmount the mounted network file syste,
	void unmountNFS();

	// Executes the shell until the user quits.
	void run();

	// Execute a script.
	void run_script(char* file_name);

private:

	// data structure for command line
	struct Command
	{
		std::string name;	// name of command
		std::string file_name;	// name of file
		std::string append_data;	// append data (append only)
	};


	bool execute_command(std::string command_str);	// Executes the command. Returns true for quit and false otherwise.
	Command parse_command(std::string command_str);	// Parses a command line into a command struct. Returned name is blank for invalid command lines.
	void mkdir_rpc(std::string dname);	// Remote procedure call on mkdir
	void cd_rpc(std::string dname);	// Remote procedure call on cd
	void home_rpc();	// Remote procedure call on home
	void rmdir_rpc(std::string dname);	// Remote procedure call on rmdir
	void ls_rpc();	// Remote procedure call on ls
	void create_rpc(std::string fname);	// Remote procedure call on create
	void append_rpc(std::string fname, std::string data);	// Remote procedure call on append
	void cat_rpc(std::string fname);	// Remote procesure call on cat
	void head_rpc(std::string fname, int n);	// Remote procedure call on head
	void rm_rpc(std::string fname);	// Remote procedure call on rm
	void stat_rpc(std::string fname);	// Remote procedure call on stat

	void SendMessageAndHandleResponse(const std::string& a_message);	// runs SendMessage and HandleResponse
	bool SendMessage(const std::string& a_message);	// sends a message to socket connection
	bool HandleResponse();	// handles response from socket connection
	void PrintResponse(const char* buf, ssize_t a_bufLen);	// prints response recieved from socket connection


	// members
	socket_t _csSock; // socket to the network file system server
	bool _isMounted; // true if the network file system is mounted, false otherise
};

#endif
