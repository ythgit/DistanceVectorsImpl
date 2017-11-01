#include <stdio.h>
#include <stdlib.h>
#include "ne.h"
#include "router.h"

int NumRoutes;
struct route_entry routingTable[MAX_ROUTERS];


void InitRoutingTbl (struct pkt_INIT_RESPONSE *InitResponse, int myID)
{
    int i = 0;
    NumRoutes = InitResponse->no_nbr + 1;
    routingTable[0].dest_id = myID;
    routingTable[0].next_hop = myID;
    routingTable[0].cost = 0;
    for (i = 0; i < NumRoutes; i++) {
        routingTable[i+1].dest_id = InitResponse->nbrcost[i].nbr;
        routingTable[i+1].next_hop = InitResponse->nbrcost[i].nbr;
        routingTable[i+1].cost = InitResponse->nbrcost[i].cost;
    }    
}

int UpdateRoutes(struct pkt_RT_UPDATE *RecvdUpdatePacket, int costToNbr, int myID)
{
    return 1;
}

void ConvertTabletoPkt(struct pkt_RT_UPDATE *UpdatePacketToSend, int myID)
{
}

void PrintRoutes (FILE* Logfile, int myID)
{
}

void UninstallRoutesOnNbrDeath(int DeadNbr)
{

}
