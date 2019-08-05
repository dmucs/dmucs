/*
 * remhost.cc: a program to change the state of a compilation host.
 *
 * Copyright (C) 2005, 2006  Victor T. Norman
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "dmucs.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <sstream>
#include <errno.h>
#include "COSMIC/HDR/sockets.h"


extern char **environ;
void usage(const char *prog);

bool debugMode = false;


int
main(int argc, char *argv[])
{
    /*
     * o Open a client socket to the server ip/port.
     * o Send my IP address in a 'remove host' or 'add host' request, depending
     *   on the name of the executable.
     * o Close the client socket.
     */

    // TODO: need to take optional ip address as argument -- to remove / add
    // host on a remote machine.

    /*
     * Process command-line arguments:
     *
     * -s <server>, --server <server>: the name of the server machine.
     * -p <port>, --port <port>: the port number to send to (default: 6714).
     * -D, --debug: debug mode (default: off)
     */
    std::ostringstream serverName;
    serverName << "@" << SERVER_MACH_NAME;
    int serverPortNum = SERVER_PORT_NUM;
    char *distingProp = NULL;

    int nextarg = 1;
    for (; nextarg < argc; nextarg++) {
	if (strequ("-s", argv[nextarg]) || strequ("--server", argv[nextarg])) {
	    if (++nextarg >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    serverName.seekp(1);     // remove everything after the first "@".
	    serverName << argv[nextarg] << '\0';
	} else if (strequ("-p", argv[nextarg]) ||
		   strequ("--port", argv[nextarg])) {
	    if (++nextarg >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    serverPortNum = atoi(argv[nextarg]);
        } else if (strequ("-t", argv[nextarg]) ||
                   strequ("--type", argv[nextarg])) {
            if (++nextarg >= argc) {
                usage(argv[0]);
                return -1;
            }
            distingProp = argv[nextarg];
	} else if (strequ("-D", argv[nextarg]) ||
		   strequ("--debug", argv[nextarg])) {
	    debugMode = true;
	} else {
	    /* We are looking at the command to run, supposedly. */
	    break;
	}
    }

    std::ostringstream clientPortStr;
    clientPortStr << "c" << serverPortNum;
    DMUCS_DEBUG((stderr, "doing Sopen with %s, %s\n",
		 serverName.str().c_str(), clientPortStr.str().c_str()));
    Socket *client_sock = Sopen((char *) serverName.str().c_str(),
				(char *) clientPortStr.str().c_str());
    if (!client_sock) {
	fprintf(stderr, "Could not open client: %s\n", strerror(errno));
	return -1;
    }

    char hostname[256];
    if (gethostname(hostname, 256) < 0) {
	fprintf(stderr, "Could not get my hostname\n");
	Sclose(client_sock);
	return -1;
    }
    struct hostent *he = gethostbyname(hostname);
    if (he == NULL) {
	fprintf(stderr, "Could not get my hostname\n");
	Sclose(client_sock);
	return -1;
    }

    struct in_addr in;
    memcpy(&in.s_addr, he->h_addr_list[0], sizeof(in.s_addr));

    /*struct sockaddr sck;
    socklen_t s = sizeof(sck);
    getsockname(client_sock->skt, &sck, &s);
    struct sockaddr_in *sin = (struct sockaddr_in *) &sck;*/

    /* If the name of the program is "addhost", then send "up" to the
       dmucs server.  Otherwise, send "down". */
    const char *op = (strstr(argv[0], "addhost") != NULL) ? "up" : "down";

    std::ostringstream clientReqStr;
    clientReqStr << "status " << inet_ntoa(in) << " " << op << " "
                 << distingProp;
    DMUCS_DEBUG((stderr, "Writing -->%s<-- to the server\n",
		 clientReqStr.str().c_str()));

    Sputs((char *) clientReqStr.str().c_str(), client_sock);

    Sclose(client_sock);

    return 0;
}



void
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-s|--server <server>] [-p|--port <port>] "
	    "[-t|--type <str>] [-D|--debug] <command> [args] \n\n", prog);
}
