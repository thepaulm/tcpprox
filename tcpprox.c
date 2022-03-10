#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <signal.h>

#define SO_MAXCONN 128

void syntax() {
	printf("tcpprox <src port> <dst port>\n");
}

struct socket_pair {
	int first;
	int second;
};

int open_listen(int port) {
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	int one = 1;
    
	int s = socket(AF_INET, SOCK_STREAM, 0);
    
	if (-1 == setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
		perror("setsockopt");
		return 0;
	}

	if (-1 == bind(s, (struct sockaddr*)&addr, sizeof(addr))) {
		printf("Failed to bind to %d\n", port);
		return 0;
	}
    
	if (-1 == listen(s, SO_MAXCONN)) {
		printf("Failed to listen\n");
		return 0;
	}
	return s;
}

/*
 * This is from UNIX Network Programming by
 * W. Richard Stevens, volume 1 second edition, pg 336.
 */
void
daemon_init(const char* pname)
{
    int i;
    pid_t pid;
    if ((pid = fork()) != 0) {
		/*
		 * Terminate parent.
		 */
		exit(0);
    }
    
    /*
     * Become session leaders.
     */
    setsid();
    
    signal(SIGHUP, SIG_IGN);
    if ((pid = fork()) != 0) {
		/*
		 * 1st child terminates.
		 */
		exit(0);
    }
    
    chdir("/");
    umask(0);
    
    for (i = 0; i < 64; i++) {
		close(i);
    }
}

/*
 * network functions.
 */
int
connect_to(const char hostname[], unsigned short port)
{
    int s;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(hostname);
    if (addr.sin_addr.s_addr == -1) {
		struct hostent* phe;
		phe = gethostbyname(hostname);
		if (!phe)
			return -1;
		addr.sin_addr.s_addr = ((struct in_addr*)(phe->h_addr))->s_addr;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    
    if (-1 == connect(s, (struct sockaddr*)&addr, sizeof(addr))) {
		printf("Failed to connect!\n");
		close(s);
		return -1;
    }
    return s;
}

int main(int argc, char *argv[]) {
	const char *localhost = "127.0.0.1";
	const char *target_host = localhost;
	int src_port = 0;
	int dst_port = 0;
	struct socket_pair pairs[128];
	int num_pairs = 0;
	int max_pairs = 0;
	fd_set set;
	int ready;
	unsigned char buffer[4096];
	int got;
	int max_sock = 0;

	memset(pairs, 0, sizeof(struct socket_pair) * 128);

	if (argc < 3) {
		syntax();
		return 0;
	}

	src_port = atoi(argv[1]);
	dst_port = atoi(argv[2]);

	if (argc > 3) {
		target_host = argv[3];
	}
	printf("Forwarding from port %d to port %d at host %s\n", src_port, dst_port, target_host);

	int sock_listen = open_listen(src_port);
	max_sock = sock_listen;
	if (sock_listen == 0) {
		return 0;
	}

	while (1) {
		/* Build our fd_set */
		FD_ZERO(&set);
		FD_SET(sock_listen, &set);
		for (int i = 0; i < max_pairs; i++) {
			if (pairs[i].first != 0) {
				FD_SET(pairs[i].first, &set);
			}
			if (pairs[i].second != 0) {
				FD_SET(pairs[i].second, &set);
			}
		}

		/* Select for infinite time */
		ready = select(max_sock + 1, &set, NULL, NULL, 0);

		/* If something was ready ... */
		if (ready != 0) {
			/* Handle listen socket */
			if (FD_ISSET(sock_listen, &set)) {
				/* Accept and connet other side */
				int s = accept(sock_listen, NULL, 0);
				if (s <= 0) {
					printf("Accepted bad socket!\n");
				} else {
					if (s > max_sock) {
						max_sock = s;
					}
					int c = connect_to(target_host, dst_port);
					if (c <= 0) {
						printf("Connected bad socket!\n");
						close(s);
					} else {
						/* Find free spot for our new pair */
						if (c > max_sock) {
							max_sock = c;
						}
						if (num_pairs == max_pairs) {
							pairs[num_pairs].first = s;
							pairs[num_pairs].second = c;
							num_pairs ++;
							max_pairs ++;
						} else {
							for (int i = 0; i < max_pairs; i++) {
								if (pairs[i].first == 0 && pairs[i].second == 0) {
									pairs[i].first = s;
									pairs[i].second = c;
									num_pairs++;
									break;
								}
							}
						}
					}
				}
			}
			/* Now run through all our pairs and see if they're in the set */
			for (int i = 0; i < max_pairs; i++) {
				int need_close = 0;

				/* if first, send to second */
				if (FD_ISSET(pairs[i].first, &set)) {
					got = recv(pairs[i].first, buffer, 4096, 0);
					if (got <= 0) {
						need_close = 1;
					} else {
						send(pairs[i].second, buffer, got, 0);
					}
				}

				/* If second, send to first */
				if (FD_ISSET(pairs[i].second, &set)) {
					got = recv(pairs[i].second, buffer, 4096, 0);
					if (got <= 0) {
						need_close = 1;
					} else {
						send(pairs[i].first, buffer, got, 0);
					}
				}
				if (need_close) {
					close(pairs[i].first);
					close(pairs[i].second);
					pairs[i].first = 0;
					pairs[i].second = 0;
					num_pairs --;
				}
			}
		}
	}
	return 0;
}
