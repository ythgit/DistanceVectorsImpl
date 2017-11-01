#include <stdio.h>
#include <string.h>
#include "ne.h"
#include "router.h"

#define DEBUG 1

int NumRoutes;
struct route_entry routingTable[MAX_ROUTERS];


void InitRoutingTbl (struct pkt_INIT_RESPONSE *InitResponse, int myID)
{
    int i = 0;
    NumRoutes = InitResponse->no_nbr + 1;
    routingTable[0].dest_id = myID;
    routingTable[0].next_hop = myID;
    routingTable[0].cost = 0;
    for (i = 0; i < NumRoutes - 1; i++) {
        routingTable[i+1].dest_id = InitResponse->nbrcost[i].nbr;
        routingTable[i+1].next_hop = InitResponse->nbrcost[i].nbr;
        routingTable[i+1].cost = InitResponse->nbrcost[i].cost;
    }    
#if DEBUG
    for (i = 0; i < MAX_ROUTERS; i++)
        printf("%s: dest_id = %d, next_hop = %d, cost = %d\n", \
                __func__, routingTable[i].dest_id, routingTable[i].next_hop, routingTable[i].cost);
#endif
}

int UpdateRoutes(struct pkt_RT_UPDATE *RecvdUpdatePacket, int costToNbr, int myID)  // dest = myID, dest not exist, split horizon rule, force update, find a longer path, find a shorter path
{
	int i,j;
	int next_cost = 0;
	for (i = 0; i < NumRoutes; i++) {
		if (RecvdUpdatePacket->dest_id == routingTable[i].dest_id) break;
	}
	if (i == 0) return 0;  // dest = myID ( this is the dest )

	for (j = 0; j < RecvdUpdatePacket->no_routes; j++) {
		if (RecvdUpdatePacket->dest_id == RecvdUpdatePacket->route[j].dest_id) break;
	}
	
	next_cost = RecvdUpdatePacket->route[j].cost;

	if (i == NumRoutes) {  // dest not exist ( new dest )
		routingTable[NumRoutes].dest_id = RecvdUpdatePacket->dest_id;
		routingTable[NumRoutes].next_hop = RecvdUpdatePacket->sender_id;
		routingTable[NumRoutes].cost = next_cost+costToNbr;
		NumRoutes++;
		return 1;
	}

	if (RecvdUpdatePacket->route[j].next_hop == myID) return 0;  // split horizon rule ( avoid infinite loop )

	if (routingTable[i].next_hop == RecvdUpdatePacket->sender_id) {  // forced update
		if (routingTable[i].cost == next_cost + costToNbr) return 0;  // unchanged
		else {
			routingTable[i].cost = next_cost + costToNbr;
			return 1;
		}
	}

	if (routingTable[i].cost <= next_cost + costToNbr) return 0; // find a longer path
	
	// find a shorter path
	routingTable[i].next_hop = RecvdUpdatePacket->sender_id;
	routingTable[i].cost = next_cost + costToNbr;

    return 1;
}

void ConvertTabletoPkt(struct pkt_RT_UPDATE *UpdatePacketToSend, int myID)
{
    UpdatePacketToSend->sender_id = myID;
    UpdatePacketToSend->no_routes = NumRoutes;
    memcpy(UpdatePacketToSend->route, routingTable, sizeof(struct route_entry) * MAX_ROUTERS);
}

void PrintRoutes (FILE* Logfile, int myID)
{
}

void UninstallRoutesOnNbrDeath(int DeadNbr)
{

}
