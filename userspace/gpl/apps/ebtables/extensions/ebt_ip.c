/*
 *  ebtables ebt_ip: IP extension module for userspace
 * 
 *  Authors:
 *   Bart De Schuymer <bdschuym@pandora.be>
 *
 *  Changes:
 *    added ip-sport and ip-dport; parsing of port arguments is
 *    based on code from iptables-1.2.7a
 *    Innominate Security Technologies AG <mhopf@innominate.com>
 *    September, 2002
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <netdb.h>
#include "../include/ebtables_u.h"
#include <linux/netfilter_bridge/ebt_ip.h>

#define IP_SOURCE '1'
#define IP_DEST   '2'
#define IP_myTOS  '3' /* include/bits/in.h seems to already define IP_TOS */
#define IP_PROTO  '4'
#define IP_SPORT  '5'
#define IP_DPORT  '6'
#define IP_myDSCP '7' /* brcm */

/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
#define IP_LENGTH '8'
#define IP_TCP_FLAGS '9'
#endif /*BUILD_MTS_QoS*/


static struct option opts[] =
{
	{ "ip-source"           , required_argument, 0, IP_SOURCE },
	{ "ip-src"              , required_argument, 0, IP_SOURCE },
	{ "ip-destination"      , required_argument, 0, IP_DEST   },
	{ "ip-dst"              , required_argument, 0, IP_DEST   },
	{ "ip-tos"              , required_argument, 0, IP_myTOS  },
	{ "ip-protocol"         , required_argument, 0, IP_PROTO  },
	{ "ip-proto"            , required_argument, 0, IP_PROTO  },
	{ "ip-source-port"      , required_argument, 0, IP_SPORT  },
	{ "ip-sport"            , required_argument, 0, IP_SPORT  },
	{ "ip-destination-port" , required_argument, 0, IP_DPORT  },
	{ "ip-dport"            , required_argument, 0, IP_DPORT  },
	{ "ip-dscp"             , required_argument, 0, IP_myDSCP }, /* brcm */
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
	/*__Verizon__, FuChia, QoS*/
	{ "ip-length"            , required_argument, 0, IP_LENGTH  },
	{ "ip-len"            , required_argument, 0, IP_LENGTH  },
        { "ip-tcp-flags"            , required_argument, 0, IP_TCP_FLAGS  },
#endif /*BUILD_MTS_QoS*/
	{ 0 }
};

/* put the ip string into 4 bytes */
static int undot_ip(char *ip, unsigned char *ip2)
{
	char *p, *q, *end;
	long int onebyte;
	int i;
	char buf[20];

	strncpy(buf, ip, sizeof(buf) - 1);

	p = buf;
	for (i = 0; i < 3; i++) {
		if ((q = strchr(p, '.')) == NULL)
			return -1;
		*q = '\0';
		onebyte = strtol(p, &end, 10);
		if (*end != '\0' || onebyte > 255 || onebyte < 0)
			return -1;
		ip2[i] = (unsigned char)onebyte;
		p = q + 1;
	}

	onebyte = strtol(p, &end, 10);
	if (*end != '\0' || onebyte > 255 || onebyte < 0)
		return -1;
	ip2[3] = (unsigned char)onebyte;

	return 0;
}

/* put the mask into 4 bytes */
static int ip_mask(char *mask, unsigned char *mask2)
{
	char *end;
	long int bits;
	uint32_t mask22;

	if (undot_ip(mask, mask2)) {
		/* not the /a.b.c.e format, maybe the /x format */
		bits = strtol(mask, &end, 10);
		if (*end != '\0' || bits > 32 || bits < 0)
			return -1;
		if (bits != 0) {
			mask22 = htonl(0xFFFFFFFF << (32 - bits));
			memcpy(mask2, &mask22, 4);
		} else {
			mask22 = 0xFFFFFFFF;
			memcpy(mask2, &mask22, 4);
		}
	}
	return 0;
}

/* set the ip mask and ip address */
void parse_ip_address(char *address, uint32_t *addr, uint32_t *msk)
{
	char *p;

	/* first the mask */
	if ((p = strrchr(address, '/')) != NULL) {
		*p = '\0';
		if (ip_mask(p + 1, (unsigned char *)msk))
			print_error("Problem with the IP mask");
	}
	else
		*msk = 0xFFFFFFFF;

	if (undot_ip(address, (unsigned char *)addr))
		print_error("Problem with the IP address");
	*addr = *addr & *msk;
}

/* transform the ip mask into a string ready for output */
char *mask_to_dotted(uint32_t mask)
{
	int i;
	static char buf[20];
	uint32_t maskaddr, bits;

	maskaddr = ntohl(mask);

	/* don't print /32 */
	if (mask == 0xFFFFFFFFL) {
		*buf = '\0';
		return buf;
	}

	i = 32;
	bits = 0xFFFFFFFEL; /* case 0xFFFFFFFF has just been dealt with */
	while (--i >= 0 && maskaddr != bits)
		bits <<= 1;

	if (i > 0)
		sprintf(buf, "/%d", i);
	else if (!i)
		*buf = '\0';
	else
		/* mask was not a decent combination of 1's and 0's */
		sprintf(buf, "/%d.%d.%d.%d", ((unsigned char *)&mask)[0],
		   ((unsigned char *)&mask)[1], ((unsigned char *)&mask)[2],
		   ((unsigned char *)&mask)[3]);

	return buf;
}

/* transform a protocol and service name into a port number */
static uint16_t parse_port(const char *protocol, const char *name)
{
	struct servent *service;
	char *end;
	int port;

	port = strtol(name, &end, 10);
	if (*end != '\0') {
		if (protocol && 
		    (service = getservbyname(name, protocol)) != NULL)
			return ntohs(service->s_port);
	}
	else if (port >= 0 || port <= 0xFFFF) {
		return port;
	}
	print_error("Problem with specified %s port '%s'", 
		    protocol?protocol:"", name);
	return 0; /* never reached */
}

static void
parse_port_range(const char *protocol, const char *portstring, uint16_t *ports)
{
	char *buffer;
	char *cp;
	
	buffer = strdup(portstring);
	if ((cp = strchr(buffer, ':')) == NULL)
		ports[0] = ports[1] = parse_port(protocol, buffer);
	else {
		*cp = '\0';
		cp++;
		ports[0] = buffer[0] ? parse_port(protocol, buffer) : 0;
		ports[1] = cp[0] ? parse_port(protocol, cp) : 0xFFFF;
		
		if (ports[0] > ports[1])
			print_error("Invalid portrange (min > max)");
	}
	free(buffer);
}

static void print_port_range(uint16_t *ports)
{
	if (ports[0] == ports[1])
		printf("%d ", ports[0]);
	else
		printf("%d:%d ", ports[0], ports[1]);
}

/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
static uint16_t parse_length(const char *name)
{
	char *end;
	int length;

	length = strtol(name, &end, 10);

	if (*end != '\0'){
		print_error("Problem with specified length '%s'", name);
		return 0; /* never reached */
	}else if (length >= 0 || length <= 0xFFFF) {
		return length;
	}	
}

static void
parse_length_range( const char *lengthstring, uint16_t *length)
{
	char *buffer;
	char *cp;
	
	buffer = strdup(lengthstring);
	if ((cp = strchr(buffer, ':')) == NULL)
		length[0] = length[1] = parse_length(buffer);
	else {
		*cp = '\0';
		cp++;
		length[0] = buffer[0] ? parse_length( buffer) : 0;
		length[1] = cp[0] ? parse_length( cp) : 0xFFFF;
		
		if (length[0] > length[1])
			print_error("Invalid lengthrange (min > max)");
	}
	free(buffer);
}

static void print_length_range(uint16_t *length)
{
	if (length[0] == length[1])
		printf("%d ", length[0]);
	else
		printf("%d:%d ", length[0], length[1]);
}

struct tcp_flag_names {
	const char *name;
	unsigned int flag;
};

static struct tcp_flag_names tcp_flag_table[]
= { { "FIN", 0x01 },
    { "SYN", 0x02 },
    { "RST", 0x04 },
    { "PSH", 0x08 },
    { "ACK", 0x10 },
    { "URG", 0x20 },
    { "ALL", 0x3F },
    { "NONE", 0 },
};

static unsigned int
parse_tcp_flag(const char *flags)
{
	unsigned int ret = 0;
	char *ptr;
	char *buffer;

	buffer = strdup(flags);

	for (ptr = strtok(buffer, ","); ptr; ptr = strtok(NULL, ",")) {
		unsigned int i;
		for (i = 0;
		     i < sizeof(tcp_flag_table)/sizeof(struct tcp_flag_names);
		     i++) {
			if (strcasecmp(tcp_flag_table[i].name, ptr) == 0) {
				ret |= tcp_flag_table[i].flag;
				break;
			}
		}
		if (i == sizeof(tcp_flag_table)/sizeof(struct tcp_flag_names))
			print_error("Unknown TCP flag `%s'", ptr);
		}

	free(buffer);
	return ret;
}

static void
parse_tcp_flags(struct ebt_ip_info *ipinfo,
		const char *mask,
		const char *cmp)
{
	ipinfo->tcp_flg_mask = parse_tcp_flag(mask);
	ipinfo->tcp_flg_cmp = parse_tcp_flag(cmp);
}

static void
print_tcpf(u_int8_t flags)
{
	int have_flag = 0;

	while (flags) {
		unsigned int i;

		for (i = 0; (flags & tcp_flag_table[i].flag) == 0; i++);

		if (have_flag)
			printf(",");
		printf("%s", tcp_flag_table[i].name);
		have_flag = 1;

		flags &= ~tcp_flag_table[i].flag;
	}

	if (!have_flag)
		printf("NONE");
}

static void
print_tcp_flags(u_int8_t mask, u_int8_t cmp)
{
	if (mask ) {
		print_tcpf(mask);
		printf(" ");
		print_tcpf(cmp);
		printf(" ");
	}
}
#endif /*BUILD_MTS_QoS*/

static void print_help()
{
	printf(
"ip options:\n"
"--ip-src    [!] address[/mask]: ip source specification\n"
"--ip-dst    [!] address[/mask]: ip destination specification\n"
"--ip-tos    [!] tos           : ip tos specification\n"
"--ip-dscp   [!] dscp          : ip dscp specification\n"
"--ip-proto  [!] protocol      : ip protocol specification\n"
"--ip-sport  [!] port[:port]   : tcp/udp source port or port range\n"
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
"--ip-dport  [!] port[:port]   : tcp/udp destination port or port range\n"
"--ip-len       [!] length[:length] : ip length or legth range\n"
"--ip-tcp-flags [!] mask comp	   : when TCP flags & mask == comp\n"
"				     (Flags: SYN ACK FIN RST URG PSH ALL NONE)\n"
);
#else
"--ip-dport  [!] port[:port]   : tcp/udp destination port or port range\n");
#endif /*BUILD_MTS_QoS*/
}

static void init(struct ebt_entry_match *match)
{
	struct ebt_ip_info *ipinfo = (struct ebt_ip_info *)match->data;

	ipinfo->invflags = 0;
	ipinfo->bitmask = 0;
}

#define OPT_SOURCE 0x01
#define OPT_DEST   0x02
#define OPT_TOS    0x04
#define OPT_PROTO  0x08
#define OPT_SPORT  0x10
#define OPT_DPORT  0x20
#define OPT_DSCP   0x40 /* brcm */

/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
#define OPT_LENGTH 0X80
#define OPT_TCP_FLAGS 0X100
#endif /*BUILD_MTS_QoS*/

static int parse(int c, char **argv, int argc, const struct ebt_u_entry *entry,
   unsigned int *flags, struct ebt_entry_match **match)
{
	struct ebt_ip_info *ipinfo = (struct ebt_ip_info *)(*match)->data;
	char *end;
	long int i;

	switch (c) {
	case IP_SOURCE:
		check_option(flags, OPT_SOURCE);
		ipinfo->bitmask |= EBT_IP_SOURCE;

	case IP_DEST:
		if (c == IP_DEST) {
			check_option(flags, OPT_DEST);
			ipinfo->bitmask |= EBT_IP_DEST;
		}
		if (check_inverse(optarg)) {
			if (c == IP_SOURCE)
				ipinfo->invflags |= EBT_IP_SOURCE;
			else
				ipinfo->invflags |= EBT_IP_DEST;
		}

		if (optind > argc)
			print_error("Missing IP address argument");
		if (c == IP_SOURCE)
			parse_ip_address(argv[optind - 1], &ipinfo->saddr,
			   &ipinfo->smsk);
		else
			parse_ip_address(argv[optind - 1], &ipinfo->daddr,
			   &ipinfo->dmsk);
		break;

	case IP_SPORT:
	case IP_DPORT:
		if (c == IP_SPORT) {
			check_option(flags, OPT_SPORT);
			ipinfo->bitmask |= EBT_IP_SPORT;
			if (check_inverse(optarg))
				ipinfo->invflags |= EBT_IP_SPORT;
		} else {
			check_option(flags, OPT_DPORT);
			ipinfo->bitmask |= EBT_IP_DPORT;
			if (check_inverse(optarg))
				ipinfo->invflags |= EBT_IP_DPORT;
		}
		if (optind > argc)
			print_error("Missing port argument");
		if (c == IP_SPORT)
			parse_port_range(NULL, argv[optind - 1], ipinfo->sport);
		else
			parse_port_range(NULL, argv[optind - 1], ipinfo->dport);
		break;

	case IP_myTOS:
		check_option(flags, OPT_TOS);
		if (check_inverse(optarg))
			ipinfo->invflags |= EBT_IP_TOS;

		if (optind > argc)
			print_error("Missing IP tos argument");
		i = strtol(argv[optind - 1], &end, 16);
		if (i < 0 || i > 255 || *end != '\0')
			print_error("Problem with specified IP tos");
		ipinfo->tos = i;
		ipinfo->bitmask |= EBT_IP_TOS;
		break;

	case IP_myDSCP:   /* brcm */
		check_option(flags, OPT_DSCP);
		if (check_inverse(optarg))
			ipinfo->invflags |= EBT_IP_DSCP;

		if (optind > argc)
			print_error("Missing IP dscp argument");
		i = strtol(argv[optind - 1], &end, 16);
		if (i < 0 || i > 255 || (i & 0x3) || *end != '\0')
			print_error("Problem with specified IP dscp");
		ipinfo->dscp = i;
		ipinfo->bitmask |= EBT_IP_DSCP;
		break;

	case IP_PROTO:
		check_option(flags, OPT_PROTO);
		if (check_inverse(optarg))
			ipinfo->invflags |= EBT_IP_PROTO;
		if (optind > argc)
			print_error("Missing IP protocol argument");
		i = strtoul(argv[optind - 1], &end, 10);
		if (*end != '\0') {
			struct protoent *pe;

			pe = getprotobyname(argv[optind - 1]);
			if (pe == NULL)
				print_error
				    ("Unknown specified IP protocol - %s",
				     argv[optind - 1]);
			ipinfo->protocol = pe->p_proto;
		} else {
			ipinfo->protocol = (unsigned char) i;
		}
		ipinfo->bitmask |= EBT_IP_PROTO;
		break;
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
	case IP_LENGTH:
		check_option(flags, OPT_LENGTH);
		if (check_inverse(optarg))
			ipinfo->invflags |= EBT_IP_LENGTH;
		if (optind > argc)
			print_error("Missing IP length argument");
		parse_length_range(argv[optind - 1], ipinfo->length);
		ipinfo->bitmask |= EBT_IP_LENGTH;
		break;
	case IP_TCP_FLAGS:
		check_option(flags, OPT_TCP_FLAGS);
		if (check_inverse(optarg))
			ipinfo->invflags |= EBT_IP_TCP_FLAGS;
		if (optind > argc)
			print_error("Missing TCP flags argument");
		parse_tcp_flags(ipinfo, argv[optind - 1], argv[optind]);
		optind++;/* Because it has two argument */
		ipinfo->bitmask |= EBT_IP_TCP_FLAGS;
		break;
#endif /*BUILD_MTS_QoS*/
	default:
		return 0;
	}
	return 1;
}

static void final_check(const struct ebt_u_entry *entry,
   const struct ebt_entry_match *match, const char *name,
   unsigned int hookmask, unsigned int time)
{
 	struct ebt_ip_info *ipinfo = (struct ebt_ip_info *)match->data;

	if (entry->ethproto != ETH_P_IP || entry->invflags & EBT_IPROTO)
		print_error("For IP filtering the protocol must be "
		            "specified as IPv4");

	if (ipinfo->bitmask & (EBT_IP_SPORT|EBT_IP_DPORT) &&
		(!(ipinfo->bitmask & EBT_IP_PROTO) || 
		ipinfo->invflags & EBT_IP_PROTO ||
		(ipinfo->protocol!=IPPROTO_TCP && 
			ipinfo->protocol!=IPPROTO_UDP)))
		print_error("For port filtering the IP protocol must be "
		            "either 6 (tcp) or 17 (udp)");
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
        if (ipinfo->bitmask & EBT_IP_TCP_FLAGS &&
		(!(ipinfo->bitmask & EBT_IP_PROTO) || 
		ipinfo->invflags & EBT_IP_PROTO ||
		ipinfo->protocol!=IPPROTO_TCP ))
		print_error("For TCP flags filtering the IP protocol must be 6 (tcp)");
#endif /*BUILD_MTS_QoS*/
}

static void print(const struct ebt_u_entry *entry,
   const struct ebt_entry_match *match)
{
	struct ebt_ip_info *ipinfo = (struct ebt_ip_info *)match->data;
	int j;

	if (ipinfo->bitmask & EBT_IP_SOURCE) {
		printf("--ip-src ");
		if (ipinfo->invflags & EBT_IP_SOURCE)
			printf("! ");
		for (j = 0; j < 4; j++)
			printf("%d%s",((unsigned char *)&ipinfo->saddr)[j],
			   (j == 3) ? "" : ".");
		printf("%s ", mask_to_dotted(ipinfo->smsk));
	}
	if (ipinfo->bitmask & EBT_IP_DEST) {
		printf("--ip-dst ");
		if (ipinfo->invflags & EBT_IP_DEST)
			printf("! ");
		for (j = 0; j < 4; j++)
			printf("%d%s", ((unsigned char *)&ipinfo->daddr)[j],
			   (j == 3) ? "" : ".");
		printf("%s ", mask_to_dotted(ipinfo->dmsk));
	}
	if (ipinfo->bitmask & EBT_IP_TOS) {
		printf("--ip-tos ");
		if (ipinfo->invflags & EBT_IP_TOS)
			printf("! ");
		printf("0x%02X ", ipinfo->tos);
	}
	if (ipinfo->bitmask & EBT_IP_PROTO) {
		struct protoent *pe;

		printf("--ip-proto ");
		if (ipinfo->invflags & EBT_IP_PROTO)
			printf("! ");
		pe = getprotobynumber(ipinfo->protocol);
		if (pe == NULL) {
			printf("%d ", ipinfo->protocol);
		} else {
			printf("%s ", pe->p_name);
		}
	}
	if (ipinfo->bitmask & EBT_IP_SPORT) {
		printf("--ip-sport ");
		if (ipinfo->invflags & EBT_IP_SPORT) {
			printf("! ");
		}
		print_port_range(ipinfo->sport);
	}
	if (ipinfo->bitmask & EBT_IP_DPORT) {
		printf("--ip-dport ");
		if (ipinfo->invflags & EBT_IP_DPORT) {
			printf("! ");
		}
		print_port_range(ipinfo->dport);
	}
   /* brcm */
	if (ipinfo->bitmask & EBT_IP_DSCP) {
		printf("--ip-dscp ");
		if (ipinfo->invflags & EBT_IP_DSCP)
			printf("! ");
		printf("0x%02X ", ipinfo->dscp);
	}
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
if (ipinfo->bitmask & EBT_IP_LENGTH) {
		printf("--ip-len ");
		if (ipinfo->invflags & EBT_IP_LENGTH) {
			printf("! ");
		}
		print_length_range(ipinfo->length);
	}
	if (ipinfo->bitmask & EBT_IP_TCP_FLAGS) {
		printf("--ip-tcp-flags ");
		if (ipinfo->invflags & EBT_IP_TCP_FLAGS) {
			printf("! ");
		}
		print_tcp_flags(ipinfo->tcp_flg_mask, ipinfo->tcp_flg_cmp);
	}
#endif /*BUILD_MTS_QoS*/
}

static int compare(const struct ebt_entry_match *m1,
   const struct ebt_entry_match *m2)
{
	struct ebt_ip_info *ipinfo1 = (struct ebt_ip_info *)m1->data;
	struct ebt_ip_info *ipinfo2 = (struct ebt_ip_info *)m2->data;

	if (ipinfo1->bitmask != ipinfo2->bitmask)
		return 0;
	if (ipinfo1->invflags != ipinfo2->invflags)
		return 0;
	if (ipinfo1->bitmask & EBT_IP_SOURCE) {
		if (ipinfo1->saddr != ipinfo2->saddr)
			return 0;
		if (ipinfo1->smsk != ipinfo2->smsk)
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_DEST) {
		if (ipinfo1->daddr != ipinfo2->daddr)
			return 0;
		if (ipinfo1->dmsk != ipinfo2->dmsk)
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_TOS) {
		if (ipinfo1->tos != ipinfo2->tos)
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_PROTO) {
		if (ipinfo1->protocol != ipinfo2->protocol)
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_SPORT) {
		if (ipinfo1->sport[0] != ipinfo2->sport[0] ||
		   ipinfo1->sport[1] != ipinfo2->sport[1])
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_DPORT) {
		if (ipinfo1->dport[0] != ipinfo2->dport[0] ||
		   ipinfo1->dport[1] != ipinfo2->dport[1])
			return 0;
	}
   /* brcm */
	if (ipinfo1->bitmask & EBT_IP_DSCP) {
		if (ipinfo1->dscp != ipinfo2->dscp)
			return 0;
	}
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__Verizon__, FuChia, QoS*/
        if (ipinfo1->bitmask & EBT_IP_LENGTH) {
		if (ipinfo1->length[0] != ipinfo2->length[0] ||
		   ipinfo1->length[1] != ipinfo2->length[1])
			return 0;
	}
	if (ipinfo1->bitmask & EBT_IP_TCP_FLAGS) {
		if (ipinfo1->tcp_flg_cmp!= ipinfo2->tcp_flg_cmp ||
		   ipinfo1->tcp_flg_mask!= ipinfo2->tcp_flg_mask)
			return 0;
	}
#endif /*BUILD_MTS_QoS*/
	return 1;
}

static struct ebt_u_match ip_match =
{
	.name		= EBT_IP_MATCH,
	.size		= sizeof(struct ebt_ip_info),
	.help		= print_help,
	.init		= init,
	.parse		= parse,
	.final_check	= final_check,
	.print		= print,
	.compare	= compare,
	.extra_ops	= opts,
};

static void _init(void) __attribute((constructor));
static void _init(void)
{
	register_match(&ip_match);
}
