/*
 * gethost.cc: The program to run to get a compilation host from the
 * DMUCS server.
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
#include <sys/socket.h>
#include <fstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <sstream>
#include <errno.h>
#include <signal.h>
#include "COSMIC/HDR/sockets.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


extern char **environ;
void usage(const char *prog);

bool debugMode = false;


void sigchld_handler(int stat)
{
    int childstat;
    wait(&childstat);
}

int
main(int argc, char *argv[])
{
    /*
     * o Open a client socket to the server ip/port.
     * o Send my IP address in a host request.
     * o Read an IP address in a response.
     * o Assign the value DISTCC_HOSTS to the IP address in the env.
     * o Use execve to run the command passed in, with its args, on the
     *   command line.
     * o Wait for the command to finish.
     * o Close the client socket.
     */

    /* install a SIGCHLD handler */
    sigset(SIGCHLD, sigchld_handler);

    /*
     * Process command-line arguments:
     *
     * -s <server>, --server <server>: the name of the server machine.
     * -p <port>, --port <port>: the port number to listen on (default: 6714).
     * -D, --debug: debug mode (default: off)
     */
    std::ostringstream serverName;
    serverName << "@" << SERVER_MACH_NAME;
    int serverPortNum = SERVER_PORT_NUM;

    int nextarg = 1;
    for (; nextarg < argc; nextarg++) {
	if (strequ("-s", argv[nextarg]) || strequ("--server", argv[nextarg])) {
	    if (++nextarg >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    serverName.seekp(1);     // remove everything after the first "@".
	    serverName << argv[nextarg] << '\0';
	} else if (strequ("-p", argv[nextarg]) || strequ("--port", argv[nextarg])) {
	    if (++nextarg >= argc) {
		usage(argv[0]);
		return -1;
	    }
	    serverPortNum = atoi(argv[nextarg]);
	} else if (strequ("-D", argv[nextarg]) || strequ("--debug", argv[nextarg])) {
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

    struct sockaddr sck;
    socklen_t s = sizeof(sck);
    getsockname(client_sock->skt, &sck, &s);
    struct sockaddr_in *sin = (struct sockaddr_in *) &sck;

    std::ostringstream clientReqStr;
    clientReqStr << "host " << inet_ntoa(in) << " " << sin->sin_port;
    DMUCS_DEBUG((stderr, "Writing -->%s<-- to the server\n",
		 clientReqStr.str().c_str()));

    Sputs((char *) clientReqStr.str().c_str(), client_sock);

    char remCompHostName[256];
    DMUCS_DEBUG((stderr, "Calling Sgets\n"));
    if (Sgets(remCompHostName, 256, client_sock) == NULL) {
	fprintf(stderr, "Got error from reading socket.\n");
	Sclose(client_sock);
	return -1;
    }
    DMUCS_DEBUG((stderr, "Got -->%s<-- from the server\n",
		 remCompHostName));

    /* If we get 0.0.0.0 that means there are no hosts left in the database.
       We will change that to the null string, and put that in for the
       DISTCC_HOSTS value. */
    std::string resolved_name;
    if (strncmp(remCompHostName, "0.0.0.0", strlen("0.0.0.0")) == 0) {
	resolved_name = "";
    } else {

	/*
	 * Convert the ip address to a hostname before putting it
	 * in the environment as the value of DISTCC_HOSTS, so that
	 * the output in the distccmon-text is nice.
	 */
	unsigned int cpuIpAddr = inet_addr(remCompHostName);
	struct in_addr c;
	c.s_addr = cpuIpAddr;
	struct hostent he, *res;
	int myerrno;
	char buffer[128];
#if HAVE_GETHOSTBYADDR_R_7_ARGS
	res = gethostbyaddr_r((char *)&cpuIpAddr, sizeof(cpuIpAddr), AF_INET,
			      &he, buffer, 128, &myerrno);
	resolved_name = (res == NULL) ? inet_ntoa(c) : he.h_name;
#elif HAVE_GETHOSTBYADDR_R_8_ARGS
	int res8 =
	    gethostbyaddr_r((char *)&cpuIpAddr, sizeof(cpuIpAddr), AF_INET,
			    &he, buffer, 128, &res, &myerrno);
	resolved_name = (res == NULL || res8 != 0) ? inet_ntoa(c) : he.h_name;
#else
#error HELP -- do not know how to compile gethostbyaddr_r
#endif

	/*
	 * Add /100 to the end of the DISTCC_HOSTS value.  This tells
	 * distcc that there are 10 cpus on the machine, which should be
	 * more than any machines already have.  Without this value, distcc
	 * assumes there are most 4 cpus, and so will not put more than 4
	 * compilations on that host at once, but instead, put the
	 * compilations in BLOCKED state.
	 *
	 * NOTE: a better solution would be to read the hosts-info file
	 * in this program and put the actual number of cpus after the '/'.
	 * But, that is alot of work for this often-run program to do, so
	 * for efficiency's sake we'll just do it this way.
	 *
	 * NOTE: even with a high value of 100 for the number of cpus,
	 * we won't overload a machine with 100 compiles, because the
	 * host-server (the 'dmucs' program) only gives out the host based
	 * on the actual number of cpus on the host -- which it gets from
	 * the hosts-info file.
	 */
	resolved_name += "/100";
    }

    

    std::ostringstream tmp;
    tmp << "DISTCC_HOSTS=" << resolved_name;
    DMUCS_DEBUG((stderr, "tmp is -->%s<--\n", tmp.str().c_str()));
    if (putenv((char *) tmp.str().c_str()) != 0) {
	fprintf(stderr, "Error putting DISTCC_HOSTS in the environment\n");
	Sclose(client_sock);
	return -1;
    }

#if 0
    for (char **ep = environ; *ep ; ep++) {
	printf("Env: %s\n", *ep);
    }
#endif

    int forkret = fork();
    if (forkret == 0) {
	/* child process */
	if (execvp(argv[nextarg], &argv[nextarg]) < 0) {
	    fprintf(stderr, "execvp failed: err %s\n", strerror(errno));
	    return -1;
	}
	return 0;
    } else if (forkret < 0) {
	fprintf(stderr, "Failed to fork a process!\n");
	return -1;
    }

    /* parent process -- just wait for the child */
    (void) wait(NULL);

    Sclose(client_sock);

    return 0;
}



void
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-s|--server <server>] [-p|--port <port>] "
	    "[-D|--debug] <command> [args] \n\n", prog);
}
