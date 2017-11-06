#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/timerfd.h>
#include <time.h>
#include <netinet/in.h>
#include <netdb.h>
#include "ne.h"
#include "router.h"

#define RETRY 10
#define DBG 1
#define NOT_UNIT_TEST 1
#define for_each_router(lp, pkt) for(lp = 0; lp < pkt.no_routes; lp++)
#define pkt_print(lp, pkt) \
					do { \
						printf("pkt sender_id = %d\n", pkt.sender_id); \
						printf("pkt dest_id = %d\n", pkt.dest_id); \
						printf("pkt no_rountes = %d\n", pkt.no_routes); \
						for(lp = 0; lp < pkt.no_routes; lp++) { \
							printf("dest_id = %d, next_hop = %d, cost = %d\n", \
									pkt.route[lp].dest_id, pkt.route[lp].next_hop, \
									pkt.route[lp].cost); }\
					} while (0);

int router_setup(int);
int ne_conn (char*, int, struct sockaddr_in *);
int init_table(int, char*, struct sockaddr*, int, int);

int main (int argc, char **argv)
{
    struct sockaddr_in neinfo = {0};
    int neport, routerport, routerid;
    int routerfd, nefd;
    int result, i;
    int nelen = sizeof(neinfo);
    char buf[PACKETSIZE];
    fd_set read_fds;
    /* timer variables */
    struct timeval timeout = {0}, from, to, start;
    /* packet variables */
	int no_nbr = 0;
    struct pkt_RT_UPDATE pkt, rcvpkt;
    char * filename;
    FILE * logptr;

    if (argc != 5) {
        perror("Usage: router <router id> <ne hostname> <ne UDP port> <router UDP port>\n"); 
        return -1;
    }

    /* parse arguments */
    routerid = atoi(argv[1]);
    routerport = atoi(argv[4]);
    neport = atoi(argv[3]);

    /* setup Network Emulator socket */
    nefd = ne_conn(argv[2], neport, &neinfo);
#if DBG
    printf("network emulator fd = %d\n", nefd);
#endif
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
#if DBG
    printf("router fd = %d\n", routerfd);
#endif

    /* Initialize the routing rable */
#if NOT_UNIT_TEST
    no_nbr = init_table(routerid, buf, (struct sockaddr*)&neinfo, nelen, nefd);
    if (no_nbr < 0) {
        perror("router initialization failed\n");
        return -1;
    }
#endif

    /* Initialize the log fd */ 
    filename = (char *)malloc(sizeof(char) * (11 + strlen(argv[1])));
    if (filename == NULL) {
        perror("failed to malloc space to string\n");
        return -1;
    }
    result = sprintf(filename, "router%d.log", routerid);
#if DBG
    printf("create a file named %s\n", filename);
#endif
    logptr = fopen(filename, "w");
    if (logptr == NULL) {
        perror("failed to create a log file descriptor\n");
        free(filename);
        return -1;
    }

    /* Initialize timer */
    result = gettimeofday(&start, NULL);
    if (logptr == NULL) {
        perror("failed to get time stamp\n");
        goto failed;
    }

    PrintRoutes(logptr, routerid);
    FD_ZERO(&read_fds);
    while(1) {
        FD_SET(nefd, &read_fds);
        timeout.tv_sec = UPDATE_INTERVAL;
        result = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);
        if(result < 0) {
            perror("select set less than 0\n");
            goto failed;
        } else if (result == 0) {
            bzero(&pkt, sizeof(struct pkt_RT_UPDATE));
            ConvertTabletoPkt(&pkt, routerid);
            for (i = 1; i <= no_nbr; i++) {
                if (pkt.route[i].dest_id == pkt.route[i].next_hop 
                       && pkt.route[i].dest_id != routerid) {
                    pkt.dest_id = pkt.route[i].dest_id;
					printf("A Packet is sent to R%d\n", pkt.route[i].dest_id);
					memcpy(&buf, &pkt, sizeof(struct pkt_RT_UPDATE));
                    hton_pkt_RT_UPDATE((struct pkt_RT_UPDATE *)&buf);
                    sendto(nefd, (const void*)&buf, sizeof(struct pkt_RT_UPDATE), 
                            0 ,(struct sockaddr*)&neinfo, nelen);
                }
            }
        } else if (FD_ISSET(nefd, &read_fds)) {
            result = recvfrom(nefd, &buf, PACKETSIZE, 0, 
                    (struct sockaddr*)&neinfo, (socklen_t *)&nelen);
            if (result <= 0) {
                perror("failed to receive packet\n");   
				continue;
            }  
            memcpy(&rcvpkt, &buf, sizeof(struct pkt_RT_UPDATE));
            ntoh_pkt_RT_UPDATE(&rcvpkt);
			printf("A Packet is received from R%d\n", rcvpkt.sender_id);
#if DBG
			pkt_print(i, rcvpkt);
#endif
            for_each_router(i, rcvpkt) {
                if (routerid == rcvpkt.route[i].dest_id) {
#if DBG
					printf("cost = %d\n", rcvpkt.route[i].cost);
#endif
                    result = UpdateRoutes(&rcvpkt, rcvpkt.route[i].cost, routerid);
                    if (result) {
                        PrintRoutes(logptr, routerid);
                        i = 0;
retry:
                        result = gettimeofday(&from, NULL);
                        if (result == -1 && i++ < RETRY) {
                            goto retry;
                        } else if (result == -1 && i == RETRY) {
                            perror("failed to get time of day\n");
                            goto failed;
                        }
                    } else {
                        result = gettimeofday(&to, NULL);
                        if (to.tv_sec - from.tv_sec >= CONVERGE_TIMEOUT) {
                            fprintf(logptr, "%d:Converged\n", (int)(to.tv_sec - start.tv_sec));
							fflush(logptr);
                        }
                    }
                    break;
                }
            }
        }
    }
    free(filename);
    fclose(logptr);
    return 0;
failed:
    free(filename);
    fclose(logptr);
    return -1;
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

    return ((struct pkt_INIT_RESPONSE *)buf)->no_nbr;
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
