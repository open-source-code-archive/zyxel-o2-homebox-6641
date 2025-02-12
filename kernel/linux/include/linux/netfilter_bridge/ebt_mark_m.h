#ifndef __LINUX_BRIDGE_EBT_MARK_M_H
#define __LINUX_BRIDGE_EBT_MARK_M_H

#define EBT_MARK_AND 0x01
#define EBT_MARK_OR 0x02
/*KeyYang@MSTC: Merge from Telefonica_Common, 20120712*/
#ifdef BUILD_MTS_QoS
/*__MTS__, FuChia,*/
#define EBT_MARK_CLASSID_CMP 0x04
#endif /*BUILD_MTS_QoS*/
#define EBT_MARK_MASK (EBT_MARK_AND | EBT_MARK_OR)
struct ebt_mark_m_info
{
	unsigned long mark, mask;
	uint8_t invert;
	uint8_t bitmask;
};
#define EBT_MARK_MATCH "mark_m"

#endif
