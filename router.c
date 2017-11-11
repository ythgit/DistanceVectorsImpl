#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
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

#define DBG 0
#define for_each_router(lp, pkt) for(lp = 0; lp < pkt.no_routes; lp++)
#define for_each_nbr(lp, no_nbr) for(lp = 0; lp < no_nbr; lp++)
#define nbr_num(lp) nbr.nbrcost[lp].nbr
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
    struct timeval timeout = {0}, start, end = {0}, intval;
    int timer = 0;
    int converge = CONVERGE_TIMEOUT;
    int nbr_tv[MAX_ROUTERS] = {0};
    /* packet variables */
    struct pkt_RT_UPDATE pkt, rcvpkt;
    struct pkt_INIT_RESPONSE nbr;
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
#if DBG
    printf("router fd = %d\n", routerfd);
#endif
    if (routerfd < 0) {
        perror("router socket failed to setup\n");
        return routerfd;
    }

    /* Initialize the routing rable */
    init_table(routerid, buf, (struct sockaddr*)&neinfo, nelen, nefd);
    memcpy(&nbr, &buf, sizeof(struct pkt_INIT_RESPONSE));

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

    /* failure detection init */
    for_each_nbr(i, nbr.no_nbr) {
        nbr_tv[i] = FAILURE_DETECTION;
    }

    /* main loop */
    PrintRoutes(logptr, routerid);
    FD_ZERO(&read_fds);
    while(1) {
        gettimeofday(&start, NULL);
        if (timeout.tv_sec < 0) {
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
        }
        FD_SET(nefd, &read_fds);
        result = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);
        if(result < 0) {
            perror("select set less than 0\n");
            goto failed;
        } else if (result == 0) {
            /* handling timer variable */
            timeout.tv_sec = UPDATE_INTERVAL;
            timer++;

            /* flood packet to neighbor */
            ConvertTabletoPkt(&pkt, routerid);
            for_each_nbr(i, nbr.no_nbr) {
                pkt.dest_id = nbr_num(i);
#if DBG
		        printf("Packet is sent to R%d\n", nbr_num(i));
#endif
		        memcpy(&buf, &pkt, sizeof(struct pkt_RT_UPDATE));
                hton_pkt_RT_UPDATE((struct pkt_RT_UPDATE *)&buf);
                sendto(nefd, (const void*)&buf, sizeof(struct pkt_RT_UPDATE), 0 ,(struct sockaddr*)&neinfo, nelen);
            }       
            /* check if neighbor is dead */
            for_each_nbr(i, nbr.no_nbr) {
                if (nbr_tv[i] > 0)
                    nbr_tv[i]--;
                if (nbr_tv[i] == 0) {
                    nbr_tv[i]--;
                    UninstallRoutesOnNbrDeath(nbr_num(i));
                    printf("Neighbor R%d is dead or link to it is down\n", nbr_num(i));
                    PrintRoutes(logptr, routerid);
                }
            }
            /* check if converged */
            if (!converge) {
                converge = -1;
                printf("%d:Converged\n", timer);
                fprintf(logptr, "%d:Converged\n", timer);
			    fflush(logptr);
            }
            /* convered timeout counter */
            if (converge > 0) 
                converge--;

        } else if (FD_ISSET(nefd, &read_fds)) {
            result = recvfrom(nefd, &buf, PACKETSIZE, 0, (struct sockaddr*)&neinfo, (socklen_t *)&nelen);
            if (result <= 0) {
                perror("failed to receive packet\n");   
				continue;
            }  
            memcpy(&rcvpkt, &buf, sizeof(struct pkt_RT_UPDATE));
            ntoh_pkt_RT_UPDATE(&rcvpkt);
			printf("A Packet is received from R%d\n", rcvpkt.sender_id);

            /* update time stamp of sender for failure detection */
            for_each_nbr(i, nbr.no_nbr) {
                if (nbr_num(i) == rcvpkt.sender_id)
                    nbr_tv[i] = FAILURE_DETECTION;
#if DBG
                printf("nbr %d has timeout %d\n", nbr_num(i), nbr_tv[i]);
#endif
            }

            /* update route table entry */
            for_each_nbr(i, nbr.no_nbr) {
                if (nbr.nbrcost[i].nbr == rcvpkt.sender_id) {
                    if (UpdateRoutes(&rcvpkt, nbr.nbrcost[i].cost, routerid)) {
                        converge = CONVERGE_TIMEOUT;
                        PrintRoutes(logptr, routerid);
                    }
                    break;
                }
            }
#if DBG
            printf("converge timeout = %d\n", converge);
#endif
        }
        gettimeofday(&end, NULL);
        /* calculate time of program execution */
        timersub(&end, &start, &intval);
        timersub(&timeout, &intval, &timeout);
#if 0
        printf("timeout sec = %ld, usec = %ld\n", timeout.tv_sec, timeout.tv_usec);
        printf("intval sec = %ld, usec = %ld\n", intval.tv_sec, intval.tv_usec);
        printf("start sec = %ld, usec = %ld\n", start.tv_sec, start.tv_usec);
        printf("end sec = %ld, usec = %ld\n", end.tv_sec, end.tv_usec);
#endif
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
