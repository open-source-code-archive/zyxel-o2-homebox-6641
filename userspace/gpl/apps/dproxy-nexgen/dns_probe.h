#ifndef _DNS_PROBE_H_
#define _DNS_PROBE_H_

/* Passive mode macros */
#define DNS_RECOVER_TIMEOUT 30

extern int dns_probe(void);
extern int dns_probe_init(void);
extern int dns_probe_is_primary_up(void);
extern void dns_probe_switchback(void);
extern int dns_probe_response(dns_request_t *m);
extern int dns_probe_activate(uint32_t name_server);
#ifndef DNS_PROBE
extern void dns_probe_set_recover_time(void);
#endif

/* WenJuin@MSTC: DNS Route, 20130320 */ 
#ifdef MSTC_TO2_GERMANY_CUSTOMIZATION
extern int load_domainRoute_table(void);
#endif /* MSTC_TO2_GERMANY_CUSTOMIZATION */

#endif /* _DNS_PROBE_H_ */
