// CPSC 3500: main
// Executes the file system program by starting the shell.

#include <iostream>  // cerr, endl
#include <cstring>  // strcmp
#include <string>  // string


#include "Shell.h"

int main(int argc, char **argv)
{
	Shell shell;

	if (argc == 2) {
		shell.mountNFS(std::string(argv[1]));
		shell.run();
	} else if (argc == 4 && std::strcmp(argv[1], "-s") == 0) {
		shell.mountNFS(std::string(argv[3]));
		shell.run_script(argv[2]);
	} else {
		std::cerr << "Invalid command line" << std::endl;
		std::cerr << "Usage (one of the following): " << std::endl;
		std::cerr << "./nfsclient server:port" << std::endl;
		std::cerr << "./nfsclient -s <script-name> server:port" << std::endl;
	}

	return 0;
}
