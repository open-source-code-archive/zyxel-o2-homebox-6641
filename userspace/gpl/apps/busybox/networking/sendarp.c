#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netinet/if_ether.h>
#include "busybox.h"

#define MAC_BCAST_ADDR	"\xff\xff\xff\xff\xff\xff"

#define IFIPADDR        1
#define IFHWADDR        2

struct arpMsg {
	struct ethhdr ethhdr;	 		/* Ethernet header */
	u_short htype;				/* hardware type (must be ARPHRD_ETHER) */
	u_short ptype;				/* protocol type (must be ETH_P_IP) */
	u_char  hlen;				/* hardware address length (must be 6) */
	u_char  plen;				/* protocol address length (must be 4) */
	u_short operation;			/* ARP opcode */
	u_char  sHaddr[6];			/* sender's hardware address */
	u_char  sInaddr[4];			/* sender's IP address */
	u_char  tHaddr[6];			/* target's hardware address */
	u_char  tInaddr[4];			/* target's IP address */
	u_char  pad[18];			/* pad for min. Ethernet payload (60 bytes) */
};

/* local prototypes */
static void sendArp(char *srcDev, char *destDev);
static void mkArpMsg(int opcode, u_long tInaddr, u_char *tHaddr, u_long sInaddr, u_char *sHaddr, struct arpMsg *msg);
static int getDevInfo (char *devname, int infotype, char *data);

int sendarp_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sendarp_main(int argc, char **argv)
{
	char *srcdev = NULL;
	char *dstdev = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "s:d:")) != -1) {
		switch (opt) {
		case 's': 
			srcdev = xstrdup(optarg);
			break;
		case 'd':
			dstdev = xstrdup(optarg);
			break;
		}
	}

    if ((srcdev == NULL) || (dstdev == NULL)) {
        bb_show_usage();
        return 0;
    }

    /* send gratutious ARP packet with srcdev's IP and hardware address to dstdev */
    sendArp(srcdev, dstdev);

	return EXIT_SUCCESS;
}

static void mkArpMsg(int opcode, u_long tInaddr, u_char *tHaddr,
		 u_long sInaddr, u_char *sHaddr, struct arpMsg *msg) {
	bzero(msg, sizeof(*msg));
	bcopy(MAC_BCAST_ADDR, msg->ethhdr.h_dest, 6); /* MAC DA */
	bcopy(sHaddr, msg->ethhdr.h_source, 6);	/* MAC SA */
	msg->ethhdr.h_proto = htons(ETH_P_ARP);	/* protocol type (Ethernet) */
	msg->htype = htons(ARPHRD_ETHER);		/* hardware type */
	msg->ptype = htons(ETH_P_IP);			/* protocol type (ARP message) */
	msg->hlen = 6;							/* hardware address length */
	msg->plen = 4;							/* protocol address length */
	msg->operation = htons(opcode);			/* ARP op code */
	*((u_int *)msg->sInaddr) = sInaddr;		/* source IP address */
	bcopy(sHaddr, msg->sHaddr, 6);			/* source hardware address */
	*((u_int *)msg->tInaddr) = tInaddr;		/* target IP address */
	if ( opcode == ARPOP_REPLY )
		bcopy(tHaddr, msg->tHaddr, 6);		/* target hardware address */
}

static int getDevInfo (char *devname, int infotype, char *data) {
    int    sock;
    struct ifreq ifr;
    int rc = 0;

    /* create device level socket */
    if ((sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0)
    {
		perror("cannot open socket ");
        return -1;
    }

    memset(&ifr, 0, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, devname);
    switch(infotype) {
        case IFIPADDR:
            /* get IP address */
            if (ioctl(sock, SIOCGIFADDR, &ifr) == -1) {
                rc = -1;
            } else {
                memcpy(data, &((struct sockaddr_in *)&(ifr.ifr_addr))->sin_addr, sizeof(struct in_addr));
            }
            break;
        case IFHWADDR:
            /* get hardware address */
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
                rc = -1;
            } else {
                memcpy(data, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
            }
            break;
        default:
            rc = -1;
            break;
    }
	close (sock);
	return rc;
}

static void sendArp(char *srcDev, char *destDev) {
    int sock;
    struct arpMsg arp;
    unsigned char br_macaddr[ETH_ALEN];
    unsigned char eth_macaddr[ETH_ALEN];
    unsigned int br_ipAddr;
    struct sockaddr_ll sll; 
    struct ifreq ifr;
    int flag;

    if ((getDevInfo(srcDev, IFIPADDR, (char *)&br_ipAddr) == 0) &&
       (getDevInfo(srcDev, IFHWADDR, (char *)br_macaddr) == 0) &&
       (getDevInfo(destDev, IFHWADDR, (char *)eth_macaddr) == 0)) {
            /* create device level socket */
            if ((sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
		        perror("cannot open socket ");
                return;
            }

            memset(&sll, 0, sizeof(sll));
            sll.sll_family = AF_PACKET;
            sll.sll_protocol = htons(ETH_P_ALL);

            /* get interface index number */
            memset(&ifr, 0, sizeof(struct ifreq));
            strcpy(ifr.ifr_name, destDev);
            if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
 		        perror("SIOCGIFINDEX(): ");
                close(sock);
                return;
            }
            sll.sll_ifindex = ifr.ifr_ifindex;
            /* bind the socket to the interface */
            if (bind(sock, (struct sockaddr *)&sll,	sizeof(sll)) == -1) {
 		        perror("bind(): ");
                close(sock);
                return;
            }
            /* set socket to non-blocking operation */
            if ((flag = fcntl(sock, F_GETFL, 0)) >= 0) {
                fcntl(sock, F_SETFL, flag | O_NONBLOCK);
            }
            mkArpMsg(ARPOP_REQUEST, br_ipAddr, NULL, br_ipAddr, br_macaddr, &arp);
            sendto(sock, &arp, sizeof(arp), 0, (struct sockaddr *)&sll, sizeof(sll));
            close(sock);
    }
}
