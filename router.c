#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <netdb.h>
#include "ne.h"
#include "router.h"

int router_setup(int);
int ne_conn (char*, int, struct sockaddr_in *);
int init_table(int, char*, struct sockaddr*, int, int);

int main (int argc, char **argv)
{
    struct sockaddr_in neinfo = {0};
    int neport, routerport, routerid;
    int routerfd, nefd;
    int result;
    int nelen = sizeof(neinfo);
    char buf[PACKETSIZE];

    if (argc != 5) {
        perror("Usage: router <router id> <ne hostname> \
                <ne UDP port> <router UDP port>\n"); 
        return -1;
    }

    /* parse arguments */
    routerid = atoi(argv[1]);
    routerport = atoi(argv[4]);
    neport = atoi(argv[3]);

    /* setup Network Emulator socket */
    nefd = ne_conn(argv[2], neport, &neinfo);
    if (nefd < 0) {
        perror("failed to connect to Network Emulator\n");
        return nefd;
    }

    /* setup router socket */
    routerfd = router_setup(routerport);
    if (routerfd < 0) {
        perror("router socket failed to setup\n");
        return routerfd;
    }

    /* Initialize the routing rable */
    result = init_table(routerid, buf, (struct sockaddr*)&neinfo, nelen, nefd);
    if (result < 0) {
        perror("router initialization failed\n");
        return -1;
    }

    return 0;
}

int init_table(int routerid, char* buf, struct sockaddr* neinfo, int len, int fd)
{
    int num;
    struct pkt_INIT_REQUEST initpkg;

    initpkg.router_id = htonl(routerid);
    memcpy(buf, &initpkg, sizeof(struct pkt_INIT_REQUEST));
    /* send */
    num = sendto(fd, buf, sizeof(struct pkt_INIT_REQUEST), 0, neinfo, len);
    if (num < 0) {
        perror("failed to send initialization request\n");   
        return -1;
    }
    /* receive */
    num = recvfrom(fd, buf, PACKETSIZE, 0, neinfo, (socklen_t *)&len);
    if (num < 0) {
        perror("failed to receive initialization request\n");   
        return -1;
    }
    /* establish table */
    ntoh_pkt_INIT_RESPONSE((struct pkt_INIT_RESPONSE *)buf);
    InitRoutingTbl((struct pkt_INIT_RESPONSE *)buf, routerid);

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

int ne_conn (char* host, int port, struct sockaddr_in * serverinfo) {
    int netfd;
    struct hostent* ip;

    netfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (netfd < 0)
        return -1;

    ip = gethostbyname(host);
    if (ip == NULL)
        return -2;

    serverinfo->sin_family = AF_INET;
    serverinfo->sin_addr = *(struct in_addr *)ip->h_addr;
    serverinfo->sin_port = htons(port);

    return netfd;

}
