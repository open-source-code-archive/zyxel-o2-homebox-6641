/* dhcpd.c
 *
 * Moreton Bay DHCP Server
 * Copyright (C) 1999 Matthew Ramsay <matthewr@moreton.com.au>
 *			Chris Trew <ctrew@moreton.com.au>
 *
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>

#include "debug.h"
#include "dhcpd.h"
#include "arpping.h"
#include "socket.h"
#include "options.h"
#include "files.h"
#include "leases.h"
#include "packet.h"
#include "serverpacket.h"
#include "pidfile.h"
#include "static_leases.h"
#include "cms_mem.h"
#include "cms_msg.h"
#ifdef DHCP_RELAY
#include "relay.h"
#endif

/* globals */
struct server_config_t server_config;
struct iface_config_t *iface_config;
struct iface_config_t *cur_iface;
#ifdef DHCP_RELAY
struct relay_config_t *relay_config;
struct relay_config_t *cur_relay;
#endif
// BRCM_begin
struct dhcpOfferedAddr *declines;
pVI_INFO_LIST viList;
void *msgHandle=NULL;
extern char deviceOui[];
extern char deviceSerialNum[];
extern char deviceProductClass[];

extern void bcmQosDhcp(int optionnum, char *cmd);
extern void bcmDelObsoleteRules(void);
extern void bcmExecOptCmd(void);

// BRCM_end

#ifdef MSTC_COMMON_PATCH // __QWEST__, Hong-Yu
struct arpEntry *cur_arp;
struct arpEntry *arp_head;
#endif

/* Exit and cleanup */
void exit_server(int retval)
{
	cmsMsg_cleanup(&msgHandle);
	pidfile_delete(server_config.pidfile);
	CLOSE_LOG();
	exit(retval);
}


/* SIGTERM handler */
static void udhcpd_killed(int sig)
{
	sig = 0;
	LOG(LOG_INFO, "Received SIGTERM");
	exit_server(0);
}

// ZyXEL, Hong-Yu
/* TR-098 DHCP Conditional Serving Pool, __TELEFONICA__, ZyXEL YungAn, 20090817. */
// __TELEFONICA__, Eric Chang
static void test_vendorid(struct dhcpMessage *packet, char *vid, int *declined)
{
	struct iface_config_t *now;
	vendor_id_t *cur_id, *now_id;
	int len;

#ifdef MSTC_COMMON_PATCH	/* For DHCP Option 60 Vendor ID Mode, __TELEFONICA__, ZyXEL Eva, 20100211. */
	int vendorIDMode = 0;
	UBOOL8 match = FALSE;
#endif

	if (vid == NULL){
		len = 0;
	}else{
		len = (int)(*((unsigned char *)vid - 1));
	}
	for (now = iface_config ; now ; now = now->next) {
		cur_id = cur_iface->vendor_ids;
		if(cur_id != NULL){
			 /*Check my(current) IF's vendor ID, Eric Chang*/
			if (strcmp(now->interface, cur_iface->interface) == 0) {  
				while (cur_id) {
#ifdef MSTC_COMMON_PATCH	/* For DHCP Option 60 Vendor ID Mode, __TELEFONICA__, ZyXEL Eva, 20100211. */
					if ( len >= cur_id->len ){
						if (strcmp(now->vendor_id_mode, "Exact") == 0)
						{
							vendorIDMode = 0;
						}
						else if (strcmp(now->vendor_id_mode, "Prefix") == 0)
						{
							vendorIDMode = 1;
	
						}
						else if (strcmp(now->vendor_id_mode, "Suffix") == 0)
						{						
							vendorIDMode = 2;
	
						}
						else if (strcmp(now->vendor_id_mode, "Substring") == 0)
						{
							vendorIDMode = 3;
						}
		

						switch (vendorIDMode){
						case 1:
							/* Prefix */
							if (memcmp(cur_id->id, vid, cur_id->len) == 0) {
								return;
							}
							break;
						case 2:
							/* Suffix */
							if (memcmp(cur_id->id, (vid + len - cur_id->len), cur_id->len) == 0) {
								return;
							}
							break;
						case 3:
							/* Substring */
							if (strstr(vid, cur_id->id)) {
								return;
							}
							break;
						case 0:
						default:	
							/* Exact */
							if (cur_id->len == len && memcmp(cur_id->id, vid, len) == 0) {
								return;
							}
						}
					}	
#endif					
					cur_id = cur_id->next;
				}
				*declined = 1;  /*Not match my vendor ID*/
				return;
 			}
		}else if(strcmp(cur_iface->interface, "br0:0") == 0){
			//cmsLog_error("[Eric]  VID = Null, If = br0:0");
			*declined = 1;  /*If device's vendor ID is null, then 2nd server should not send offer.*/
			return;
		}else{
			/*Check other IF's vendor ID, Eric Chang*/
			if (strcmp(now->interface, cur_iface->interface) != 0) {   
			now_id = now->vendor_ids;
			while (now_id) {
#ifdef MSTC_COMMON_PATCH	/* For DHCP Option 60 Vendor ID Mode, __TELEFONICA__, ZyXEL Eva, 20100211. */
				if ( len >= now_id->len ){
					if (strcmp(now->vendor_id_mode, "Exact") == 0)
					{
						vendorIDMode = 0;
					}		
					else if (strcmp(now->vendor_id_mode, "Prefix") == 0)
					{
						vendorIDMode = 1;
	
					}
					else if (strcmp(now->vendor_id_mode, "Suffix") == 0)
					{
						vendorIDMode = 2;
	
					}
					else if (strcmp(now->vendor_id_mode, "Substring") == 0)
					{
						vendorIDMode = 3;
		
					}
				
					switch (vendorIDMode){
					case 1:
						if (memcmp(now_id->id, vid, now_id->len) == 0) {
							match = TRUE;
						}
						break;
					case 2:
						if (memcmp(now_id->id, (vid + len - now_id->len), now_id->len) == 0) {
							match = TRUE;
						}
						break;
					case 3:
						if (strstr(vid, now_id->id)) {
							match = TRUE;
						}
						break;
					case 0:
					default:	
				if (now_id->len == len && memcmp(now_id->id, vid, len) == 0) {
							match = TRUE;
						}								
					}
				}
				if (match) {
#endif					
					*declined = 1;  /*Match other's vendor ID, I won't send offer nor ack.*/
					if(!strstr(cur_iface->interface, "condservpool")){
						memcpy(declines->chaddr, packet->chaddr, 16);
						declines->mac_addr[0] = 0;
						declines->clientid[0] = 0;
						declines->vsi[0] = 0;
						declines->vendorid[0] = 0;
						strcpy(declines->vendorid, now_id->id);
						write_decline();
					}
					return;
				}
				now_id = now_id->next;
			    }
		           }
		    }
	}
	return;
}

static void test_macAddr(struct dhcpMessage *packet, int *declined)
{
	struct iface_config_t *now;
	mac_t *mac;
	int len;
	char client_mac[20];

	sprintf(client_mac, "%02x:%02x:%02x:%02x:%02x:%02x", packet->chaddr[0], packet->chaddr[1], packet->chaddr[2],
		packet->chaddr[3], packet->chaddr[4], packet->chaddr[5]);
	len = strlen(client_mac);
	for (now = iface_config ; now ; now = now->next) {
		if (strcmp(now->interface, cur_iface->interface) != 0) {
			mac = now->macs;
			while (mac) {
				if (mac->len == len && strncasecmp(mac->mac, client_mac, len) == 0) {
					*declined = 1;
					memcpy(declines->chaddr, packet->chaddr, 16);
					declines->vendorid[0] = 0;
					declines->clientid[0] = 0;
					declines->vsi[0] = 0;
					strcpy(declines->mac_addr, mac->mac);
					write_decline();
					return;
				}

				mac = mac->next;
			}
		}
	}

	return;
}

static void test_clientid(struct dhcpMessage *packet, char *client_id, int *declined)
{
	struct iface_config_t *now;
	clnt_id_t *id;
	int len, type;
	char optionData[128], *dataPtr, tmp[32];

	if ((client_id == NULL) || ((unsigned char) (*client_id) != 0xff)) return;
	len = (int)(*((unsigned char *)client_id - 1));
	dataPtr = client_id + 1;
	sprintf(optionData, "%d", (int)(*((unsigned int *)dataPtr))); // IAID
	dataPtr += CLIENT_IDENTIFIER_IAID_INFO_LEN;
	type = (int) (*(unsigned short *)(dataPtr));
	if (type == VENDOR_DUID_LLT) {
		dataPtr += 2;
		sprintf(optionData, "%s|llt|%d", optionData, *(unsigned short *)(dataPtr));
		dataPtr += 2;
		sprintf(optionData, "%s|%d", optionData, *(unsigned int *)(dataPtr));
		dataPtr += 4;
		sprintf(optionData, "%s|%02x:%02x:%02x:%02x:%02x:%02x", optionData, ((unsigned char)(*(dataPtr + 0))), 
			((unsigned char)(*(dataPtr + 1))), ((unsigned char)(*(dataPtr + 2))), 
			((unsigned char)(*(dataPtr + 3))), ((unsigned char)(*(dataPtr + 4))), 
			((unsigned char)(*(dataPtr + 5))));
	} else if (type == VENDOR_DUID_EN) {
		dataPtr += 2;
		sprintf(optionData, "%s|en|%d", optionData, *(unsigned int *)(dataPtr));
		dataPtr += 4;
		strncpy(tmp, dataPtr, len - 11); // 11 = FF+IAID+DUID type+EN
		tmp[len - 11] = 0;
		sprintf(optionData, "%s|%s", optionData, tmp);
	} else if (type == VENDOR_DUID_LL) {
		dataPtr += 2;
		sprintf(optionData, "%s|ll|%d", optionData, *(unsigned short *)(dataPtr));
		dataPtr += 2;
		sprintf(optionData, "%s|%02x:%02x:%02x:%02x:%02x:%02x", optionData, ((unsigned char)(*(dataPtr + 0))), 
			((unsigned char)(*(dataPtr + 1))), ((unsigned char)(*(dataPtr + 2))), 
			((unsigned char)(*(dataPtr + 3))), ((unsigned char)(*(dataPtr + 4))), 
			((unsigned char)(*(dataPtr + 5))));
	} else {
		strncpy(tmp, dataPtr, len - 5); // 5 = FF+IAID
		tmp[len - 5] = 0;
		sprintf(optionData, "%s|other|%s", optionData, tmp);
	}

	len = strlen(optionData);
	for (now = iface_config ; now ; now = now->next) {
		if (strcmp(now->interface, cur_iface->interface) != 0) {
			id = now->clnt_ids;
			while (id) {
				if (strcmp(id->id, optionData) == 0) {
					*declined = 1;
					memcpy(declines->chaddr, packet->chaddr, 16);
					declines->mac_addr[0] = 0;
					declines->vendorid[0] = 0;
					declines->vsi[0] = 0;
					strcpy(declines->clientid, id->id);
					write_decline();
					return;
				}

				id = id->next;
			}
		}
	}

	return;
}

static void test_vsi(struct dhcpMessage *packet, char *vsi_info, int *declined)
{
	struct iface_config_t *now;
	vsi_t *info;
	int len;
	char optionData[128], *dataPtr, tmp[32];

	if (vsi_info == NULL) return;
	dataPtr = vsi_info;
	sprintf(optionData, "%d", *(unsigned int*) (dataPtr));
	dataPtr += (VENDOR_ENTERPRISE_LEN + 3);
	len = (int) (*((unsigned char *)dataPtr - 1));
	strncpy(tmp, dataPtr, len);
	tmp[len] = 0;
	sprintf(optionData, "%s|%s", optionData, tmp);
	dataPtr += (len + 2);
	len = (int) (*((unsigned char *)dataPtr - 1));
	strncpy(tmp, dataPtr, len);
	tmp[len] = 0;
	sprintf(optionData, "%s|%s", optionData, tmp);
	dataPtr += (len + 2);
	len = (int) (*((unsigned char *)dataPtr - 1));
	strncpy(tmp, dataPtr, len);
	tmp[len] = 0;
	sprintf(optionData, "%s|%s", optionData, tmp);
	dataPtr += (len + 2);
	len = (int) (*((unsigned char *)dataPtr - 1));
	strncpy(tmp, dataPtr, len);
	tmp[len] = 0;
	sprintf(optionData, "%s|%s", optionData, tmp);

	len = strlen(optionData);
	for (now = iface_config ; now ; now = now->next) {
		if (strcmp(now->interface, cur_iface->interface) != 0) {
			info = now->vsi;
			while (info) {
				if (strcmp(info->vsi, optionData) == 0) {
					*declined = 1;
					memcpy(declines->chaddr, packet->chaddr, 16);
					declines->mac_addr[0] = 0;
					declines->vendorid[0] = 0;
					declines->clientid[0] = 0;
					strcpy(declines->vsi, info->vsi);
					write_decline();
					return;
				}

				info = info->next;
			}
		}
	}

	return;
}


#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
static void test_source_port(struct dhcpMessage *packet, int *declined, int *condition_matched)
{
	struct iface_config_t *now;
	source_port_t *source_port;
	int len;
	
	char client_mac[32] = {0};
	char client_port[32] = {0};

	int i;
	char cmd[BUFLEN_128];

	sprintf(client_mac, "%02x:%02x:%02x:%02x:%02x:%02x", 
		packet->chaddr[0], packet->chaddr[1], packet->chaddr[2],
		packet->chaddr[3], packet->chaddr[4], packet->chaddr[5]);

	/* Scan MAC addresses from br0 to br5 */
	for(i = 0; i <= 5; i++){
		if(i == 0){
		    sprintf(cmd, "brctl showmacs br%d 1>/var/br_mac_ifname_table 2>/dev/null", i);
		}else{
		    sprintf(cmd, "brctl showmacs br%d 1>>/var/br_mac_ifname_table 2>/dev/null", i);
		}
		system(cmd);
	}

	/* Lookup the table to find the port name of this MAC */
	FILE* fs = fopen("/var/br_mac_ifname_table", "r");
	char line[512];
	char tblPortNo[32];
	char tblMacAddr[32];
	char tblIsLocal[32];
	char tblAging[32];
	char tblIfName[32];
	if(fs){
		while(fgets(line, sizeof(line), fs)){
			sscanf(line, "%s %s %s %s %s", tblPortNo, tblMacAddr, tblIsLocal, tblAging, tblIfName);
			if(strcmp(client_mac, tblMacAddr) == 0){
				strcpy(client_port, tblIfName);
				break;
			}
		}
		fclose(fs);
	}
   
	/* If port is not found, do not decline this request */
	if(client_port[0] == 0){
		return;
	}

	/* Scan all other interface setting, determine if current interface should decline. */
	len = strlen(client_port);
	
	for (now = iface_config ; now ; now = now->next) {
      //printf("[DHCP]%d, now->interface=%s,cur_iface->interface=%s\n",__LINE__,now->interface,cur_iface->interface);
		if (strcmp(now->interface, cur_iface->interface) != 0) {
			source_port = now->source_port;
			while (source_port) {
#ifdef MSTC_COMMON_PATCH	/* Fix the bug that can not obtain IP from the Conditional Serving Pool according to the port name due to the match mode is "exact", __TELEFONICA__, ZyXEL Eva, 20101124. */			
				if (strstr(client_port, source_port->source_port)) {
#else					
				if ((source_port->len == len) && strncasecmp(source_port->source_port, client_port, len) == 0) {
#endif					
					*declined = 1;
					/* Does conditional pool need decline list? */
					if(!strstr(cur_iface->interface, "condservpool")){
						memcpy(declines->chaddr, packet->chaddr, 16);
						declines->vendorid[0] = 0;
						declines->clientid[0] = 0;
						declines->vsi[0] = 0;
						declines->mac_addr[0] = 0;
						strcpy(declines->source_port, source_port->source_port);
						write_decline();
					}
					return;
				}
				source_port = source_port->next;
			}
		}else{
			/* Match my pool, if I should server some ports, determine if I should serve it? */
			source_port = now->source_port;
			if(source_port){
				while (source_port) {
               //printf("[DHCP]%d, source_port->len=%d,source_port->source_port=%s<=>client_port=%s\n",__LINE__,source_port->len,source_port->source_port,client_port);
#ifdef MSTC_COMMON_PATCH	/* Fix the bug that can not obtain IP from the Conditional Serving Pool according to the port name due to the match mode is "exact", __TELEFONICA__, ZyXEL Eva, 20101124. */						
				    if (strstr(client_port, source_port->source_port)) {
#else	
					if ((source_port->len == len) && strncasecmp(source_port->source_port, client_port, len) == 0) {
#endif
						*condition_matched = 1;
						return;  /*Match my source port*/
					}
					source_port = source_port->next;
				}
				
				*declined = 1;  /*Not match my source port*/
				/* Does conditional pool need decline list? */
				if(!strstr(cur_iface->interface, "condservpool")){
					memcpy(declines->chaddr, packet->chaddr, 16);
					declines->vendorid[0] = 0;
					declines->clientid[0] = 0;
					declines->vsi[0] = 0;
					declines->mac_addr[0] = 0;
					strcpy(declines->source_port, source_port->source_port);
					write_decline();
				}
				return;
			}
		}
	}
	return;
}
#endif

#ifdef DHCP_QOS_OPTION
void bcmQosDhcp(int ignored)
{
   key_t key;
   int msgid;
   struct optionmsgbuf msgbuf;
   char classname[16];
   char cmd[256];
   struct optioncmd *pnode;
   key = ftok("/var", 'm');
   /* Open the queue - create if necessary */
   if((msgid = msgget(key, IPC_CREAT|0660)) == -1) {
        perror("msgget");
        exit(1);
   }
   if( (read_message(msgid, &msgbuf, 1)) > 0){
   	strcpy(classname, msgbuf.mtext);
	read_message(msgid, &msgbuf, 1);
	strcpy(cmd, msgbuf.mtext);
	switch(bcmParseCmdAction(cmd)){
		case 'A':
		case 'I':
			if( !bcmInsertOptCmd(classname, cmd))
				printf("malloc error for insert the rule\r\n");
			break;
		case 'D':
			bcmHandleDeleteRule(classname, cmd);
			return;
		default:
			printf("wrong command\r\n");
			return;
		}
      bcmOptcmdloop();
   }
//   bcmOptcmdloop();
}
#endif

#ifdef MSTC_COMMON_PATCH // __QWEST__, Hong-Yu
void handleStaticHosts(u_int8_t *chaddr)
{
   getArpInfo();
   checkStaticHosts(chaddr);
   freeArpInfo();
}
#endif


// BRCM_END

#ifdef COMBINED_BINARY	
int udhcpd(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
	CmsRet ret;
	int msg_fd;
	fd_set rfds;
	struct timeval tv;
        //BRCM --initialize server_socket to -1
	int bytes, retval;
	struct dhcpMessage packet;
	unsigned char *state;
        // BRCM added vendorid
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
	char tmpHostName[32];
        char client_mac[20];
#endif
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
	char tmpLog[512] = {0};
#endif
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
    char *server_id, *requested, *hostname, *vendorid = NULL, *clientid = NULL, *vsi = NULL;
#endif
	u_int32_t server_id_align, requested_align;
	unsigned long timeout_end;
	struct dhcpOfferedAddr *lease;
	//For static IP lease
	struct dhcpOfferedAddr static_lease;
	uint32_t static_lease_ip;

	int pid_fd;
	/* Optimize malloc */
	mallopt(M_TRIM_THRESHOLD, 8192);
	mallopt(M_MMAP_THRESHOLD, 16384);
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
      int usable_sockets = 0;  /*Eric Chang*/
#endif	

	//argc = argv[0][0]; /* get rid of some warnings */

	OPEN_LOG("udhcpd");
	LOG(LOG_INFO, "udhcp server (v%s) started", VERSION);
	
	pid_fd = pidfile_acquire(server_config.pidfile);
	pidfile_write_release(pid_fd);

	if ((ret = cmsMsg_init(EID_DHCPD, &msgHandle)) != CMSRET_SUCCESS) {
		LOG(LOG_ERR, "cmsMsg_init failed, ret=%d", ret);
		pidfile_delete(server_config.pidfile);
		CLOSE_LOG();
		exit(1);
	}

	//read_config(DHCPD_CONF_FILE);
	read_config(argc < 2 ? DHCPD_CONF_FILE : argv[1]);
	
	read_leases(server_config.lease_file);

        // BRCM begin
	declines = malloc(sizeof(struct dhcpOfferedAddr));
	/* vendor identifying info list */
	viList = malloc(sizeof(VI_INFO_LIST));
	memset(viList, 0, sizeof(VI_INFO_LIST));
        deviceOui[0] = '\0';
        deviceSerialNum[0] = '\0';
        deviceProductClass[0] = '\0';
        // BRCM end

#ifndef DEBUGGING
	pid_fd = pidfile_acquire(server_config.pidfile); /* hold lock during fork. */

#ifdef BRCM_CMS_BUILD
   /*
    * BRCM_BEGIN: mwang: In CMS architecture, we don't want dhcpd to
    * daemonize itself.  It creates an extra zombie process that
    * we don't want to deal with.
    */
#else
	switch(fork()) {
	case -1:
		perror("fork");
		exit_server(1);
		/*NOTREACHED*/
	case 0:
		break; /* child continues */
	default:
		exit(0); /* parent exits */
		/*NOTREACHED*/
		}
	close(0);
	setsid();
#endif /* BRCM_CMS_BUILD */

	pidfile_write_release(pid_fd);
#endif
	signal(SIGUSR1, write_leases);
	signal(SIGTERM, udhcpd_killed);
	signal(SIGUSR2, write_viTable);

	timeout_end = time(0) + server_config.auto_time;

	cmsMsg_getEventHandle(msgHandle, &msg_fd);

	while(1) { /* loop until universe collapses */
                //BRCM_begin
                int declined = 0;
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
      int condition_matched = 0;
#endif	
		int max_skt = msg_fd;
		FD_ZERO(&rfds);
		FD_SET(msg_fd, &rfds);
#ifdef DHCP_RELAY
                for(cur_relay = relay_config; cur_relay;
			cur_relay = cur_relay->next ) {
			if (cur_relay->skt < 0) {
				cur_relay->skt = listen_socket(INADDR_ANY,
					SERVER_PORT, cur_relay->interface);
				if(cur_relay->skt == -1) {
					LOG(LOG_ERR, "couldn't create relay "
						"socket");
					exit_server(0);
				}
			}

			FD_SET(cur_relay->skt, &rfds);
			if (cur_relay->skt > max_skt)
				max_skt = cur_relay->skt;
		}
#endif
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool

     /*Create socket for all interfaces.*/
   for(cur_iface = iface_config; cur_iface;cur_iface = cur_iface->next ) {
     if ( (cur_iface->server != 0) && (cur_iface->skt < 0)) { 
        cur_iface->skt = listen_socket(INADDR_ANY,
           SERVER_PORT, cur_iface->UsedIf);
        if(cur_iface->skt == -1) {
           LOG(LOG_ERR, "couldn't create server "
              "socket on %s -- au revoir",
              cur_iface->interface);
        }
     }
     if(cur_iface->skt > 0 || (!strcmp(cur_iface->interface, "br0") && cur_iface->skt == 0 )){
        //cmsLog_error("[Eric] DHCP, If = %s, cur_iface->skt = %d",cur_iface->interface, cur_iface->skt);
        usable_sockets++;
        FD_SET(cur_iface->skt, &rfds);
        if (cur_iface->skt > max_skt)
           max_skt = cur_iface->skt;
      }
   } 

   if(usable_sockets == 0){
     LOG(LOG_ERR, "No server socket can be created.");
     exit_server(0);
   }
   
#endif
		if (server_config.auto_time) {
			tv.tv_sec = timeout_end - time(0);
			if (tv.tv_sec <= 0) {
				tv.tv_sec = server_config.auto_time;
				timeout_end = time(0) + server_config.auto_time;
				write_leases(0);
			}
			tv.tv_usec = 0;
		}
		retval = select(max_skt + 1, &rfds, NULL, NULL,
			server_config.auto_time ? &tv : NULL);
		if (retval == 0) {
			write_leases(0);
			timeout_end = time(0) + server_config.auto_time;
			continue;
		} else if (retval < 0) {
			if (errno != EINTR)
				perror("select()");
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
         for(cur_iface = iface_config; cur_iface;cur_iface = cur_iface->next ) {
				close(cur_iface->skt);
				cur_iface->skt = -1;
			}
#endif
			continue;
		}

		/* Is there a CMS message received? */
		if (FD_ISSET(msg_fd, &rfds)) {
			CmsMsgHeader *msg;
			CmsRet ret;
			ret = cmsMsg_receiveWithTimeout(msgHandle, &msg, 0);
			if (ret == CMSRET_SUCCESS) {
#ifdef MSTC_COMMON_PATCH //__ZYXEL__, Autumn prevent response
            if ( !msg->flags_response ) {
#endif

            switch (msg->type) {
            case CMS_MSG_DHCPD_RELOAD:
					/* Reload config file */
					write_leases(0);
					read_config(argc < 2 ? DHCPD_CONF_FILE : argv[1]);
					read_leases(server_config.lease_file);
               break;
            case CMS_MSG_WAN_CONNECTION_UP:
#ifdef DHCP_RELAY
					/* Refind local addresses for relays */
					set_relays();
#endif
               break;
            case CMS_MSG_GET_LEASE_TIME_REMAINING:
               if (msg->dataLength == sizeof(GetLeaseTimeRemainingMsgBody)) {
					   GetLeaseTimeRemainingMsgBody *body = (GetLeaseTimeRemainingMsgBody *) (msg + 1);
					   u_int8_t chaddr[16]={0};

#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
/* Fix stl_lanHostEntryObject hanging issue, __TELEFONICA__, ZyXEL YungAn, 20090824. */
                  UBOOL8 leaseFound = FALSE;
                  cur_iface = find_iface_by_ifname(body->poolName);
#endif
					   if (cur_iface != NULL) {
						   cmsUtl_macStrToNum(body->macAddr, chaddr);
						   lease = find_lease_by_chaddr(chaddr);
						// __Verizon__, Hong-Yu
						if (lease != NULL)
							msg->wordData = lease_time_remaining(lease);
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
/* Fix stl_lanHostEntryObject hanging issue, __TELEFONICA__, ZyXEL YungAn, 20090824. */
							leaseFound = TRUE;
							//fprintf(stderr, "Lease info found in pool %s!\n", cur_iface->interface); //ZyXEL YungAn debug only
#endif							
   					}
   					else
						   msg->wordData = 0;

   					msg->dst = msg->src;
   					msg->src = EID_DHCPD;
   					msg->flags_request = 0;
   					msg->flags_response = 1;
   					msg->dataLength = 0;
   					cmsMsg_send(msgHandle, msg);

#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
/* Fix stl_lanHostEntryObject hanging issue, __TELEFONICA__, ZyXEL YungAn, 20090824. */
					/* Return lease time zero if cannot find lease. */
					if(!leaseFound){
						//fprintf(stderr, "No lease info found!\n"); //ZyXEL YungAn debug only
						msg->dst = msg->src;
						msg->src = EID_DHCPD;
						msg->flags_request = 0;
						msg->flags_response = 1;
						msg->wordData = 0;
						msg->dataLength = 0;
						cmsMsg_send(msgHandle, msg);
					}
#endif
               }
               else {
                  LOG(LOG_ERR, "invalid msg, type=0x%x dataLength=%d", msg->type, msg->dataLength);
               }
               break;
            case CMS_MSG_EVENT_SNTP_SYNC:               
               if (msg->dataLength == sizeof(long)) 
               {
                  long *delta = (long *) (msg + 1); 
                  adjust_lease_time(*delta);
               }
               else
               {
                  LOG(LOG_ERR, "Invalid CMS_MSG_EVENT_SNTP_SYNC msg, dataLength=%d", msg->dataLength);
               }
               break;
            case CMS_MSG_QOS_DHCP_OPT60_COMMAND:
               bcmQosDhcp(DHCP_VENDOR, (char *)(msg+1));
               break;
            case CMS_MSG_QOS_DHCP_OPT77_COMMAND:
               bcmQosDhcp(DHCP_USER_CLASS_ID, (char *)(msg+1));
               break;
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
            case CMS_MSG_DHCPD_HOST_INFO:
               if (msg->dataLength == sizeof(DhcpdHostInfoMsgBody)) {
                  handleHostInfoUpdate(msg);
               }
               else {
                  LOG(LOG_ERR, "invalid msg, type=0x%x dataLength=%d", msg->type, msg->dataLength);
               }
               break;
#endif
            default:
               LOG(LOG_ERR, "unrecognized msg, type=0x%x dataLength=%d", msg->type, msg->dataLength);
               break;
            }
#ifdef MSTC_COMMON_PATCH //__ZYXEL__, Autumn prevent response
            }
#endif
				cmsMem_free(msg);
			} else if (ret == CMSRET_DISCONNECTED) {
				LOG(LOG_ERR, "lost connection to smd, "
					"exiting now.");
				exit_server(0);
			}
			continue;
		}
#ifdef DHCP_RELAY
		/* Check packets from upper level DHCP servers */
		for (cur_relay = relay_config; cur_relay;
			cur_relay = cur_relay->next) {
			if (FD_ISSET(cur_relay->skt, &rfds))
				break;
		}
		if (cur_relay) {
			process_relay_response();
			continue;
		}
#endif

		/* Find out which interface is selected */
		for(cur_iface = iface_config; cur_iface;
			cur_iface = cur_iface->next ) {
			if (FD_ISSET(cur_iface->skt, &rfds))
				break;
		}
		if (cur_iface == NULL)
			continue;

		bytes = get_packet(&packet, cur_iface->skt); /* this waits for a packet - idle */
		if(bytes < 0)
			continue;

		/* Decline requests */
		if (cur_iface->decline)
			continue;

#ifdef DHCP_RELAY
		/* Relay request? */
		if (cur_iface->relay_interface[0]) {
			process_relay_request((char*)&packet, bytes);
			continue;
		}
#endif

		if((state = get_option(&packet, DHCP_MESSAGE_TYPE)) == NULL) {
			DEBUG(LOG_ERR, "couldnt get option from packet -- ignoring");
			continue;
		}
		
		/* For static IP lease */
		/* Look for a static lease */
		static_lease_ip = getIpByMac(cur_iface->static_leases, &packet.chaddr);

#if 1 /*For To2 fix ipaddress by device name, Mitrastar ChingLung, 20130301*/
		if(!static_lease_ip)
		{
			int hostname_len;
			char *hostname_str;
			hostname_str  = (char *)get_option(&packet, DHCP_HOST_NAME);
			if(hostname_str){
				hostname_len = (UINT32)hostname_str[-1];
				char *hostname_align = xmalloc(hostname_len);
				hostname_align[hostname_len]='\0';
				memcpy( hostname_align, hostname_str, hostname_len);
				static_lease_ip = getIpByDevname(cur_iface->static_leases_devnames, hostname_align);
				free(hostname_align);
			}
		}
#endif
		if(static_lease_ip)
		{
			memcpy(&static_lease.chaddr, &packet.chaddr, 16);
			static_lease.yiaddr = static_lease_ip;
			static_lease.expires = -1; /* infinite lease time */
			lease = &static_lease;
		} /* For static IP lease end */
		else
		{
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
			handleStaticHosts(packet.chaddr);
			sprintf(client_mac, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned char) packet.chaddr[0], 
				(unsigned char) packet.chaddr[1], (unsigned char) packet.chaddr[2], 
				(unsigned char) packet.chaddr[3], (unsigned char) packet.chaddr[4], 
				(unsigned char) packet.chaddr[5]);
#endif
			lease = find_lease_by_chaddr(packet.chaddr);
		}
		switch (state[0]) {
		case DHCPDISCOVER:
			DEBUG(LOG_INFO,"received DISCOVER");
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive DHCP DISCOVER from %s", client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif

			//BRCM_begin
			vendorid = (char *)get_option(&packet, DHCP_VENDOR);
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
			if (!declined){
				test_source_port(&packet, &declined, &condition_matched);
			}
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
			if (!condition_matched && !declined && (vendorid!=NULL)){
#endif
				test_vendorid(&packet, vendorid, &declined);
			}
#endif

#ifndef MSTC_COMMON_PATCH /* TR-098 DHCP Conditional Serving Pool, __TELEFONICA__, ZyXEL Eva, 20101124. */	

// ZyXEL, Hong-Yu
			clientid = get_option(&packet, DHCP_CLIENT_ID);
			vsi = get_option(&packet, DHCP_VENDOR_IDENTIFYING);

			/* for dynamic interface grouping */
			if (!declined)
				test_macAddr(&packet, &declined);
			if (!declined && clientid != NULL)
				test_clientid(&packet, clientid, &declined);
			if (!declined && vsi != NULL)
				test_vsi(&packet, vsi, &declined);
			if (cur_iface->filter == 1) break;

#endif
         if (!declined) {
				if (sendOffer(&packet) < 0) {
					LOG(LOG_DEBUG, "send OFFER failed -- ignoring");
				}
//brcm begin
            /* delete any obsoleted QoS rules because sendOffer() may have
             * cleaned up the lease data.
             */
            bcmDelObsoleteRules(); 
//brcm end
			}
			break;			
 		case DHCPREQUEST:
			DEBUG(LOG_INFO,"received REQUEST");
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive DHCP REQUEST from %s", client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif
			requested = (char *)get_option(&packet, DHCP_REQUESTED_IP);
			server_id = (char *)get_option(&packet, DHCP_SERVER_ID);
			hostname  = (char *)get_option(&packet, DHCP_HOST_NAME);
			if (requested) memcpy(&requested_align, requested, 4);
			if (server_id) memcpy(&server_id_align, server_id, 4);
		
			//BRCM_begin
			vendorid = (char *)get_option(&packet, DHCP_VENDOR);
#ifdef MSTC_COMMON_PATCH //__Qwest__, SinJia, TR-098 DHCP Conditional Serving Pool
         if (!declined){
            test_source_port(&packet, &declined, &condition_matched);
         }
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
         if (!condition_matched && !declined && (vendorid!=NULL)){
#endif
            test_vendorid(&packet, vendorid, &declined);
         }
#endif

#ifndef MSTC_COMMON_PATCH /* TR-098 DHCP Conditional Serving Pool, __TELEFONICA__, ZyXEL Eva, 20101124. */
// ZyXEL, Hong-Yu
			clientid = get_option(&packet, DHCP_CLIENT_ID);
			vsi = get_option(&packet, DHCP_VENDOR_IDENTIFYING);

			/* for dynamic interface grouping */
			if (!declined)
				test_macAddr(&packet, &declined);
			if (!declined && clientid != NULL)
				test_clientid(&packet, clientid, &declined);
			if (!declined && vsi != NULL)
				test_vsi(&packet, vsi, &declined);
			if (cur_iface->filter == 1) break;
#endif
			if (!declined) {
				if (lease) {
					if (hostname) {
						bytes = hostname[-1];
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
						if (bytes >= (int) sizeof(tmpHostName))
							bytes = sizeof(tmpHostName) - 1;
						strncpy(tmpHostName, hostname, bytes);
						tmpHostName[bytes] = '\0';
//						fprintf(stderr, "host name: %s\n", tmpHostName);
						if (cmsUtl_isInVaildDomainName(tmpHostName))
							strcpy(tmpHostName, "unknown");
#endif
					}  else
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
						strcpy(tmpHostName, "unknown");           
#endif
					if (server_id) {
						/* SELECTING State */
						DEBUG(LOG_INFO, "server_id = %08x", ntohl(server_id_align));
						if (server_id_align == cur_iface->server && requested && 
						    requested_align == lease->yiaddr) {
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
							lease = sendACK(&packet, lease->yiaddr);
							strcpy(lease->hostname, tmpHostName);
#endif
							send_lease_info(FALSE, lease);
//brcm begin
                     /* delete any obsoleted QoS rules because sendACK() may have
                      * cleaned up the lease data.
                      */
                     bcmDelObsoleteRules(); 
                     bcmExecOptCmd(); 
//brcm end
#ifndef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
							send_flash_update();
#endif
						}
					} else {
						if (requested) {
							/* INIT-REBOOT State */
							if (lease->yiaddr == requested_align) {
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
								lease = sendACK(&packet, lease->yiaddr);
								strcpy(lease->hostname, tmpHostName);
#endif
								send_lease_info(FALSE, lease);
//brcm begin
                        /* delete any obsoleted QoS rules because sendACK() may have
                         * cleaned up the lease data.
                         */
                        bcmDelObsoleteRules(); 
                        bcmExecOptCmd(); 
//brcm end
#ifndef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
								send_flash_update();
#endif
							}
							else sendNAK(&packet);
						} else {
							/* RENEWING or REBINDING State */
							if (lease->yiaddr == packet.ciaddr) {
#ifdef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
								lease = sendACK(&packet, lease->yiaddr);
								strcpy(lease->hostname, tmpHostName);
#endif
								send_lease_info(FALSE, lease);
//brcm begin
                        /* delete any obsoleted QoS rules because sendACK() may have
                         * cleaned up the lease data.
                         */
                        bcmDelObsoleteRules(); 
                        bcmExecOptCmd(); 
//brcm end
#ifndef MSTC_COMMON_PATCH // __Verizon__, Hong-Yu
								send_flash_update();
#endif
							}
							else {
								/* don't know what to do!!!! */
								sendNAK(&packet);
							}
						}						
					}
				} else { /* else remain silent */				
    					sendNAK(&packet);
            			}
			}
			break;
		case DHCPDECLINE:
			DEBUG(LOG_INFO,"received DECLINE");
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive DHCP DECLINE from %s", client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif

#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
			if (cur_iface->filter == 1) break;
#endif
			if (lease) {
				memset(lease->chaddr, 0, 16);
				lease->expires = time(0) + server_config.decline_time;
			}			
			break;
		case DHCPRELEASE:
			DEBUG(LOG_INFO,"received RELEASE");
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive DHCP RELEASE from %s", client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
			if (cur_iface->filter == 1) break;
#endif
			if (lease) {
				lease->expires = time(0);
				send_lease_info(TRUE, lease);
//brcm begin
            sleep(1);
            /* delete the obsoleted QoS rules */
            bcmDelObsoleteRules(); 
//brcm end
			}
			break;
		case DHCPINFORM:
#ifdef MTS_Telefonica_TR69
      {     
			char *req = NULL;
			struct dhcpOfferedAddr leaseTmp;			
#endif
			DEBUG(LOG_INFO,"received INFORM");
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive DHCP INFORM from %s", client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
			if (cur_iface->filter == 1) break;
#endif

#ifdef MTS_Telefonica_TR69  /*Telefonica, Jason, 20100611*/
			send_inform(&packet);
			/* if DHCPRequest from client has device identity, send back gateway identity,
				and save the device identify */
			if (req = get_option(&packet, DHCP_VENDOR_IDENTIFYING)) {
				if (saveVIoption(req, &leaseTmp) < 0) {
					LOG(LOG_ERR, "Vendor-Specific Information parsing fail");
				}
				send_VIoption_info(FALSE, &leaseTmp);
			}
      }
#else
			send_inform(&packet);
#endif
			break;	
		default:
			LOG(LOG_WARNING, "unsupported DHCP message (%02x) -- ignoring", state[0]);
#ifdef MSTC_COMMON_PATCH /* Follow 11n model to support dhcpd log. __TELEFONICA__, MSTC Stan Su, 20120220. */
			tmpLog[0] = '\0';
			sprintf(tmpLog, "Receive unsupported DHCP message (%02x) from %s", state[0], client_mac);
			oalLog_syslog(LOG_LEVEL_NOTICE, tmpLog);
#endif
		}
	}
	return 0;
}

