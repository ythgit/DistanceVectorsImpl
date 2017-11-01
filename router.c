#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "ne.h"
#include "router.h"

int router_setup(int);

int main (int argc, char **argv)
{
    int neport, routerport, routerid;
    int routerfd;
    if (argc != 5) {
        printf("Usage: router <router id> <ne hostname> \
                <ne UDP port> <router UDP port>\n"); 
        return -1;
    }

    /* parse arguments */
    routerid = atoi(argv[1]);
    routerport = atoi(argv[4]);
    neport = atoi(argv[3]);

    /* setup router socket */
    routerfd = router_setup(routerport);
    if (routerfd < 0)
        printf("router socket failed to setup\n");

    return 0;
}

int router_setup(int port)
{
    int listenfd, optval = 1;
    struct sockaddr_in serveraddr = {0};

    listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listenfd < 0) {
        fprintf(stderr, "could not establish socket connection\n");
        return -1;
    }
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int)) < 0)
        return -1;

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons((unsigned short)port);
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    return listenfd; 
}
