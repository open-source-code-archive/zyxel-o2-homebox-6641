/* 
 * options.c -- DHCP server option packet tools 
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "dhcpd.h"
#include "files.h"
#include "options.h"
#include "leases.h"
#include "cms_msg.h"
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
#include "dhcpc.h"
#endif

/* supported options are easily added here */
struct dhcp_option options[] = {
	/* name[10]	flags				code */
	{"subnet",	OPTION_IP,			0x01},
	{"timezone",	OPTION_S32,			0x02},
	{"router",	OPTION_IP | OPTION_LIST,	0x03},
	{"timesvr",	OPTION_IP | OPTION_LIST,	0x04},
	{"namesvr",	OPTION_IP | OPTION_LIST,	0x05},
	{"dns",		OPTION_IP | OPTION_LIST,	0x06},
	{"logsvr",	OPTION_IP | OPTION_LIST,	0x07},
	{"cookiesvr",	OPTION_IP | OPTION_LIST,	0x08},
	{"lprsvr",	OPTION_IP | OPTION_LIST,	0x09},
	{"hostname",	OPTION_STRING,			0x0c},
	{"bootsize",	OPTION_U16,			0x0d},
	{"domain",	OPTION_STRING,			0x0f},
	{"swapsvr",	OPTION_IP,			0x10},
	{"rootpath",	OPTION_STRING,			0x11},
	{"ipttl",	OPTION_U8,			0x17},
	{"mtu",		OPTION_U16,			0x1a},
	{"broadcast",	OPTION_IP,			0x1c},
	{"ntpsrv",	OPTION_IP | OPTION_LIST,	0x2a},
	{"vendinfo", OPTION_STRING, 0x2b},
	{"wins",	OPTION_IP | OPTION_LIST,	0x2c},
	{"requestip",	OPTION_IP,			0x32},
	{"lease",	OPTION_U32,			0x33},
	{"dhcptype",	OPTION_U8,			0x35},
	{"serverid",	OPTION_IP,			0x36},
	{"tftp",	OPTION_STRING,			0x42},
	{"bootfile",	OPTION_STRING,			0x43},
	{"6rd",	OPTION_STRING,			DHCP_6RD_OPT},
#ifdef MSTC_COMMON_PATCH	/* DHCP option 60, second DHCP server enhancement, __TELEFONICA__, ZyXEL YungAn, 20090424. */
	{"128",	OPTION_STRING,	0x80},
	{"134",	OPTION_U16,		0x86},
	{"135",	OPTION_U16,		0x87},
	{"240",	OPTION_STRING,	0xf0},
	{"241",	OPTION_STRING,	0xf1},
	{"242",	OPTION_STRING,	0xf2},
	{"243",	OPTION_STRING,	0xf3},
	{"244",	OPTION_STRING,	0xf4},
	{"245",	OPTION_STRING,	0xf5},
#endif
#ifdef SUPPORT_IPV6_6RD //__Common__, DingRuei, add 6RD option
	{"6rd",	OPTION_6RD,			0xd4},
#endif
	{"",		0x00,				0x00}
};

/* Lengths of the different option types */
int option_lengths[] = {
	[OPTION_IP] =		4,
	[OPTION_IP_PAIR] =	8,
#ifdef SUPPORT_IPV6_6RD //__Common__, DingRuei, 6RD option length
	[OPTION_6RD] =	22,
#endif
	[OPTION_BOOLEAN] =	1,
	[OPTION_STRING] =	0,
	[OPTION_U8] =		1,
	[OPTION_U16] =		2,
	[OPTION_S16] =		2,
	[OPTION_U32] =		4,
	[OPTION_S32] =		4
};

/* these are device info which are query once and store globally */
char deviceOui[VENDOR_GATEWAY_OUI_MAX_LEN];
char deviceSerialNum[VENDOR_GATEWAY_SERIAL_NUMBER_MAX_LEN];
char deviceProductClass[VENDOR_GATEWAY_PRODUCT_CLASS_MAX_LEN];

/* get an option with bounds checking (warning, not aligned). */
unsigned char *get_option(struct dhcpMessage *packet, int code)
{
	int i, length;
	static char err[] = "bogus packet, option fields too long."; /* save a few bytes */
	unsigned char *optionptr = NULL;
	int over = 0, done = 0, curr = OPTION_FIELD;
	
	optionptr = packet->options;
	i = 0;
//Amy
#if 1 // __TELEFONICA__, Eric Chang
	length = 1024;
#else
	length = 308;
#endif
	while (!done) {
		if (i >= length) {
			LOG(LOG_WARNING, err);
			return NULL;
		}
		if (optionptr[i + OPT_CODE] == code) {
			if (i + 1 + optionptr[i + OPT_LEN] >= length) {
				LOG(LOG_WARNING, err);
				return NULL;
			}
			return optionptr + i + 2;
		}			
		switch (optionptr[i + OPT_CODE]) {
		case DHCP_PADDING:
			i++;
			break;
		case DHCP_OPTION_OVER:
			if (i + 1 + optionptr[i + OPT_LEN] >= length) {
				LOG(LOG_WARNING, err);
				return NULL;
			}
			over = optionptr[i + 3];
			i += optionptr[OPT_LEN] + 2;
			break;
		case DHCP_END:
			if (curr == OPTION_FIELD && over & FILE_FIELD) {
				optionptr = packet->file;
				i = 0;
				length = 128;
				curr = FILE_FIELD;
			} else if (curr == FILE_FIELD && over & SNAME_FIELD) {
				optionptr = packet->sname;
				i = 0;
				length = 64;
				curr = SNAME_FIELD;
			} else done = 1;
			break;
		default:
			i += optionptr[OPT_LEN + i] + 2;
		}
	}
	return NULL;
}


/* return the position of the 'end' option (no bounds checking) */
int end_option(unsigned char *optionptr) 
{
	int i = 0;
	
	while (optionptr[i] != DHCP_END) {
		if (optionptr[i] == DHCP_PADDING) i++;
		else i += optionptr[i + OPT_LEN] + 2;
	}
	return i;
}


/* add an option string to the options (an option string contains an option code,
 * length, then data) */
int add_option_string(unsigned char *optionptr, unsigned char *string)
{
	int i, end = end_option(optionptr);
	
	/* end position + string length + option code/length + end option */
//#ifdef TELEFONICA_FEATURE /* 20080912 Louie modify for DHCP Option */
#ifdef MSTC_COMMON_PATCH	// __TELEFONICA__, Eric Chang
	if (end + string[OPT_LEN] + 2 + 1 >= 1024) {
#else
	if (end + string[OPT_LEN] + 2 + 1 >= 308) {
#endif
		for (i = 0; options[i].code && options[i].code != string[OPT_CODE]; i++);
		LOG(LOG_ERR, "Option %s (0x%02x) did not fit into the packet!", 
			options[i].code ? options[i].name : "unknown", string[OPT_CODE]);
		return 0;
	}
	DEBUG(LOG_INFO, "adding option 0x%02x", string[OPT_CODE]);
	memcpy(optionptr + end, string, string[OPT_LEN] + 2);
	optionptr[end + string[OPT_LEN] + 2] = DHCP_END;
	return string[OPT_LEN] + 2;
}


/* add a one to four byte option to a packet */
int add_simple_option(unsigned char *optionptr, unsigned char code, u_int32_t data)
{
	char length = 0;
	int i, end;
	char buffer[4]; /* Cant copy straight to optionptr, it might not be aligned */

	for (i = 0; options[i].code; i++)
		if (options[i].code == code) {
			length = option_lengths[options[i].flags & TYPE_MASK];
			break;
		}
		
	if (!length) {
		DEBUG(LOG_ERR, "Could not add option 0x%02x", code);
		return 0;
	}
	
	DEBUG(LOG_INFO, "adding option 0x%02x", code);
	end = end_option(optionptr);
	optionptr[end + OPT_CODE] = code;
	optionptr[end + OPT_LEN] = length;

	switch (length) {
		case 1: buffer[0] = (char) data; break;
		case 2: *((u_int16_t *) buffer) = htons(data); break;
		case 4: *((u_int32_t *) buffer) = htonl(data); break;
	}
	memcpy(&optionptr[end + 2], buffer, length);
	optionptr[end + length + 2] = DHCP_END;
	return length;
}


/* find option 'code' in opt_list */
struct option_set *find_option(struct option_set *opt_list, char code)
{
	while (opt_list && opt_list->data[OPT_CODE] < code)
		opt_list = opt_list->next;

	if (opt_list && opt_list->data[OPT_CODE] == code) return opt_list;
	else return NULL;
}


/* add an option to the opt_list */
void attach_option(struct option_set **opt_list, struct dhcp_option *option, char *buffer, int length)
{
	struct option_set *existing, *new, **curr;

	/* add it to an existing option */
	if ((existing = find_option(*opt_list, option->code))) {
		DEBUG(LOG_INFO, "Attaching option %s to existing member of list", option->name);
		if (option->flags & OPTION_LIST) {
			if (existing->data[OPT_LEN] + length <= 255) {
				existing->data = realloc(existing->data, 
						existing->data[OPT_LEN] + length + 2);
				memcpy(existing->data + existing->data[OPT_LEN] + 2, buffer, length);
				existing->data[OPT_LEN] += length;
			} /* else, ignore the data, we could put this in a second option in the future */
		} /* else, ignore the new data */
	} else {
		DEBUG(LOG_INFO, "Attaching option %s to list", option->name);
		
		/* make a new option */
		new = malloc(sizeof(struct option_set));
		new->data = malloc(length + 2);
		new->data[OPT_CODE] = option->code;
		new->data[OPT_LEN] = length;
		memcpy(new->data + 2, buffer, length);
		
		curr = opt_list;
		while (*curr && (*curr)->data[OPT_CODE] < option->code)
			curr = &(*curr)->next;
			
		new->next = *curr;
		*curr = new;		
	}
}


//brcm begin
void viInfoFree(pVI_OPTION_INFO pInfo)
{
  if (pInfo) {
    free(pInfo->oui);
    free(pInfo->serialNumber);
    free(pInfo->productClass);
    free(pInfo);
  }
}

void addViToList(pVI_OPTION_INFO pNew) {
  pVI_OPTION_INFO pPtr= NULL;

  /* if VI exists already, don't add, just update the info */
  if (viList->count > 0) {
    pPtr = viList->pHead;
    while (pPtr) {
      if (pPtr->ipAddr == pNew->ipAddr) {
	/* found it, just copy the info over */
	memcpy(pPtr,pNew,sizeof(VI_OPTION_INFO));
	viInfoFree(pNew);
	return;
      }
      pPtr = pPtr->next;
    } /* while */
  } /* list has something */

  if (viList->pHead == NULL) {
    viList->pHead = pNew;
    viList->pTail = pNew;
  }
  else {
    viList->pTail->next = pNew;
    viList->pTail = pNew;
  }
  viList->count++;
}

void viListFree(void)
{
  pVI_OPTION_INFO pInfo;

  while (viList->pHead) {
    pInfo = viList->pHead;
    viList->pHead = viList->pHead->next;
    free(pInfo->oui);
    free(pInfo->serialNumber);
    free(pInfo->productClass);
    free(pInfo);
  }
  viList->count = 0;
}
int CreateClntId(char *iaid, char *duid, char *out_op)
{
  unsigned char optionData[256], *dataPtr;
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  int sub_length;
  char tmpInfo[64], *start, *end, type[8];
#else
  char *vp;
#endif
  int value, i, length;


  /* dhcp option 61, rfc4361.txt section 6.1 
         Code  Len  Type  IAID                DUID
       +----+----+-----+----+----+----+----+----+----+---
       | 61 | n  | 255 | i1 | i2 | i3 | i4 | d1 | d2 |...
       +----+----+-----+----+----+----+----+----+----+---

       DUID-EN, rfc3315, section 9.3
     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |               2               |       enterprise-number       |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |   enterprise-number (contd)   |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
    .                           identifier                          .
    .                       (variable length)                       .
    .                                                               .
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       
  */
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  optionData[0] = DHCP_CLIENT_ID; // option code 61
  optionData[2] = CLIENT_IDENTIFIER_TYPE; // type = 255
  length = CLIENT_IDENTIFIER_TYPE_LEN;
  sub_length = 0;
  dataPtr = optionData + CLIENT_IDENTIFIER_IAID_INFO_OFFSET;

  // fill in the 4-byte IAID value
  *(unsigned int*)(dataPtr) = (unsigned int) atoi(iaid);
  dataPtr += CLIENT_IDENTIFIER_IAID_INFO_LEN;
  length += CLIENT_IDENTIFIER_IAID_INFO_LEN;
  // fill in the DUID value
  start = duid;
  end = strstr(duid, ",");
  strcpy(tmpInfo, end + 1);
  strncpy(type, start, (size_t)(end - start));
  type[end - start] = '\0';
  if (!strcmp(type, "other")) {
	sub_length = strlen(end + 1);
	strcpy(dataPtr, end + 1);
	dataPtr += sub_length;
	length += sub_length;
  } else if (!strcmp(type, "en")) {
	/* DUID type */
	*(unsigned short*)(dataPtr) = (unsigned short) VENDOR_DUID_EN;
	dataPtr+= 2;
	length += 2;
	/* Enterprise Number */
	start = strtok(tmpInfo, ",");
        *(unsigned int*) (dataPtr) = (unsigned int) atoi(start);
	dataPtr += VENDOR_ENTERPRISE_LEN;
	length += VENDOR_ENTERPRISE_LEN;
        /* Identifier */
	start = strtok(NULL, ",");
	sub_length = strlen(start);
	strcpy(dataPtr, start);
	dataPtr += sub_length;
	length += sub_length;
  } else if (!strcmp(type, "ll")) {
  	/* DUID type */
  	*(unsigned short*)(dataPtr) = (unsigned short) VENDOR_DUID_LL;
  	dataPtr+= 2;
  	length += 2;
  	/* hardware type */
  	*(unsigned short*)(dataPtr) = (unsigned short) VENDOR_HARDWARE_TYPE;
  	dataPtr+= 2;
  	length += 2;

  	/* link-layer address */
	start = strtok(tmpInfo, ":");
	while(start) {
		*dataPtr++ = (unsigned char)((asciiToHex(start[0])) * 16 + (asciiToHex(start[1])));
		length++;
		start = strtok(NULL, ":");
	}
  }
#else
  optionData[0] = 61;
  optionData[2] = 0xff;
  dataPtr = optionData+3;
  //here fill in the IAID
  vp = iaid;
  while ( sscanf(vp, "%2x%n", &value, &i) == 1 )
  {
      *dataPtr++ = value;
      vp += i;
  }
  
  //here fill in the client DUID value 
  *dataPtr++ = 0;
  *dataPtr++ = 2;
  vp = duid;
  while ( sscanf(vp, "%2x%n", &value, &i) == 1 )
  {
      *dataPtr++ = value;
      vp += i;
  }
  length = (unsigned)(dataPtr - optionData)-2;
#endif
  optionData[1] = (unsigned char)length;
  memcpy((void*)out_op, (void*)optionData, length+2);
  return 0;
}

#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
int asciiToHex(char token)
{
	if(token >= '0' && token <= '9') return token - 48;
	if(token >= 'a' && token <= 'f') return token - 87;
	if(token >= 'A' && token <= 'F') return token - 55;

	return -1;
}
#endif

#ifdef MSTC_COMMON_PATCH// ZyXEL, Hong-Yu
int createOption125(int type, char *VIinfo)
#else
int CreateOption125(int type, char *oui, char *sn, char *prod, char *VIinfo)
#endif
{
  char optionData[VENDOR_IDENTIFYING_INFO_LEN], *dataPtr;
  int len, totalLen = 0;
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
#else
  char line[VENDOR_IDENTIFYING_INFO_LEN];
#endif

  optionData[VENDOR_OPTION_CODE_OFFSET] = (char)VENDOR_IDENTIFYING_OPTION_CODE;
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  *(unsigned int*)(optionData+VENDOR_OPTION_ENTERPRISE_OFFSET) = (unsigned int)VENDOR_ZYXEL_ENTERPRISE_NUMBER;
#else
  *(unsigned int*)(optionData+VENDOR_OPTION_ENTERPRISE_OFFSET) = (unsigned int)VENDOR_BRCM_ENTERPRISE_NUMBER;
#endif
  dataPtr = optionData + VENDOR_OPTION_DATA_OFFSET + VENDOR_OPTION_DATA_LEN;
  totalLen = VENDOR_ENTERPRISE_LEN + VENDOR_OPTION_DATA_LEN;
  /* read system information and add it to option data */
  /* OUI */
  if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
    *dataPtr++ = (char)VENDOR_DEVICE_OUI_SUBCODE;
  else 
    *dataPtr++ = (char)VENDOR_GATEWAY_OUI_SUBCODE;
  len = strlen(oui);
  *dataPtr++ = len;
  strncpy(dataPtr,oui,len);
  dataPtr += len;
  totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);
  
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  /* Product Class */
  if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
    *dataPtr++ = (char)VENDOR_DEVICE_PRODUCT_CLASS_SUBCODE;
  else
    *dataPtr++ = (char)VENDOR_GATEWAY_PRODUCT_CLASS_SUBCODE;
  len = strlen(product_class);
  *dataPtr++ = len;
  strncpy(dataPtr, (const char*)product_class, len);
  dataPtr += len;
  totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);

  /* Model Name */
  if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
    *dataPtr++ = VENDOR_DEVICE_MODEL_NAME_SUBCODE;
  else 
    *dataPtr++ = VENDOR_GATEWAY_MODEL_NAME_SUBCODE;
  len = strlen(model_name);
  *dataPtr++ = len;
  strncpy(dataPtr, model_name,len);
  dataPtr += len;
  totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);
#endif

  /* Serial Number */
  if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
    *dataPtr++ = (char)VENDOR_DEVICE_SERIAL_NUMBER_SUBCODE;
  else
    *dataPtr++ = (char)VENDOR_GATEWAY_SERIAL_NUMBER_SUBCODE;
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  len = strlen(serial_number);
#else
  len = strlen(sn);
#endif
  *dataPtr++ = len;
#ifdef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  strncpy(dataPtr,(const char*)serial_number,len);
#else
  strncpy(dataPtr,(const char*)sn,len);
#endif
  dataPtr += len;
  totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);

#ifndef MSTC_COMMON_PATCH // ZyXEL, Hong-Yu
  /* Product Class */
  if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
    *dataPtr++ = VENDOR_DEVICE_PRODUCT_CLASS_SUBCODE;
  else 
    *dataPtr++ = VENDOR_GATEWAY_PRODUCT_CLASS_SUBCODE;
  len = strlen(prod);
  *dataPtr++ = len;
  strncpy(dataPtr,(const char*)prod,len);
  dataPtr += len;
  totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);
#endif

  optionData[VENDOR_OPTION_LEN_OFFSET] = totalLen;
  optionData[VENDOR_OPTION_DATA_OFFSET] = totalLen - VENDOR_ENTERPRISE_LEN - VENDOR_OPTION_DATA_LEN;

  /* also copy the option code and option len which is not counted in total len */
  memcpy((void*)VIinfo,(void*)optionData,(totalLen+VENDOR_OPTION_DATA_OFFSET));

  return 0;
}

/* BRCM begin:
   This function query the system/device information (serial number, product class...)
*/
CmsRet queryDeviceInfo(GetDeviceInfoMsgBody *pDeviceInfo)
{
   CmsRet ret = CMSRET_SUCCESS;
   CmsMsgHeader *msg;
   void *msgBuf;

   msgBuf = cmsMem_alloc(sizeof(CmsMsgHeader), ALLOC_ZEROIZE);
   msg = (CmsMsgHeader *)msgBuf;
   msg->type = CMS_MSG_GET_DEVICE_INFO;
   msg->src = EID_DHCPD;
   msg->dst = EID_SSK;
   msg->flags_request = 1;

   if ((ret = cmsMsg_send(msgHandle, msg)) != CMSRET_SUCCESS)
   {
      cmsLog_error("Failed to send message (ret=%d)", ret);
   }
   else
   {
      CMSMEM_FREE_BUF_AND_NULL_PTR(msg);
//Amy
#ifdef MSTC_COMMON_PATCH /* Enlarge timeout value of receive msg, __TELEFONICA__, Mitrastar Stan Su, 20110324. */
	  if ((ret = cmsMsg_receiveWithTimeout(msgHandle, &msg, 140)) != CMSRET_SUCCESS)
#else
      if ((ret = cmsMsg_receiveWithTimeout(msgHandle, &msg, 10)) != CMSRET_SUCCESS)
#endif
      {
         cmsLog_error("Failed to receive message (ret=%d)", ret);
      }
      else
      {
//Amy
#ifndef SUPPORT_TELEFONICA	/* Get the DeviceInfo from response for DHCP option 125 packets, __TELEFONICA__, Mitrastar Eva, 20111007. */
#ifdef MSTC_COMMON_PATCH //__ZYXEL__, Autumn prevent response
         if ( !msg->flags_response ) {
#endif
#endif
         switch(msg->type)
         {
         case CMS_MSG_GET_DEVICE_INFO:
            if(msg->dataLength != sizeof(GetDeviceInfoMsgBody))
            {
               cmsLog_error("Invalid Data Length: received %d != expected %d",
                            msg->dataLength, sizeof(GetDeviceInfoMsgBody));
               
               ret = CMSRET_INTERNAL_ERROR;
               break;
            }
            msgBuf = (GetDeviceInfoMsgBody *)(msg + 1);
            memcpy(pDeviceInfo,msgBuf,sizeof(GetDeviceInfoMsgBody));
            break;
              
         default:
            cmsLog_error("Invalid message type (%x), expects CMS_MSG_GET_DEVICE_INFO (%x)", (unsigned int)msg->type,CMS_MSG_GET_DEVICE_INFO);
            ret = CMSRET_INTERNAL_ERROR;
         }
//Amy
#ifndef SUPPORT_TELEFONICA	/* __TELEFONICA__, Mitrastar Eva, 20111007. */
#ifdef MSTC_COMMON_PATCH //__ZYXEL__, Autumn prevent response
         }
#endif
#endif
         CMSMEM_FREE_BUF_AND_NULL_PTR(msg);
      } /* else rx ok */
   }
   return (ret);
} /* queryDeviceInfo */


/* this function generates VI info based on TR111 part I.
   If -1 is return, there is no VI info found.  Otherwise, 0 is returned.
   Type specifies gateway vendor info or device vendor info.   
   VIinfo is where the option string is stored */
int createVIoption(int type, char *VIinfo)
{
  char optionData[VENDOR_IDENTIFYING_INFO_LEN], *dataPtr;
  int len, totalLen = 0;
  GetDeviceInfoMsgBody deviceInfo;

  if (deviceOui[0] == '\0')
  {
     if (queryDeviceInfo(&deviceInfo) != CMSRET_SUCCESS)
     {
        return -1;
     }
     else
     {
        strncpy(deviceOui,deviceInfo.oui,VENDOR_GATEWAY_OUI_MAX_LEN);
        strncpy(deviceSerialNum,deviceInfo.serialNum,VENDOR_GATEWAY_SERIAL_NUMBER_MAX_LEN);
        strncpy(deviceProductClass,deviceInfo.productClass,VENDOR_GATEWAY_PRODUCT_CLASS_MAX_LEN);
     }
  }

     optionData[VENDOR_OPTION_CODE_OFFSET] = (char)VENDOR_IDENTIFYING_OPTION_CODE;
#ifdef MTS_Telefonica_TR69	/* The enterprise number should be 3561 (ADSL Forum) , __TELEFONICA__, ZyXEL Eva, 20100630. */
	 *(unsigned int*)(optionData+VENDOR_OPTION_ENTERPRISE_OFFSET) = (unsigned int)VENDOR_ADSL_FORUM_ENTERPRISE_NUMBER;	
#else
     *(unsigned int*)(optionData+VENDOR_OPTION_ENTERPRISE_OFFSET) = (unsigned int)VENDOR_BRCM_ENTERPRISE_NUMBER;
#endif
     dataPtr = optionData + VENDOR_OPTION_DATA_OFFSET + VENDOR_OPTION_DATA_LEN;
     totalLen = VENDOR_ENTERPRISE_LEN + VENDOR_OPTION_DATA_LEN;
     /* read system information and add it to option data */
     /* OUI */
     if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
        *dataPtr++ = (char)VENDOR_DEVICE_OUI_SUBCODE;
     else 
        *dataPtr++ = (char)VENDOR_GATEWAY_OUI_SUBCODE;
  len = strlen(deviceOui);
     *dataPtr++ = len;
  strncpy(dataPtr,deviceOui,len);
     dataPtr += len;
     totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);
  
     /* Serial Number */
     if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
        *dataPtr++ = (char)VENDOR_DEVICE_SERIAL_NUMBER_SUBCODE;
     else
        *dataPtr++ = (char)VENDOR_GATEWAY_SERIAL_NUMBER_SUBCODE;
  len = strlen(deviceSerialNum);
     *dataPtr++ = len;
  strncpy(dataPtr,(const char*)deviceSerialNum,len);
     dataPtr += len;
     totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);

     /* Product Class */
     if (type == VENDOR_IDENTIFYING_FOR_DEVICE)
        *dataPtr++ = VENDOR_DEVICE_PRODUCT_CLASS_SUBCODE;
     else 
        *dataPtr++ = VENDOR_GATEWAY_PRODUCT_CLASS_SUBCODE;
  len = strlen(deviceProductClass);
     *dataPtr++ = len;
  strncpy(dataPtr,(const char*)deviceProductClass,len);
     dataPtr += len;
     totalLen += (len + VENDOR_SUBCODE_AND_LEN_BYTES);

     optionData[VENDOR_OPTION_LEN_OFFSET] = totalLen;
     optionData[VENDOR_OPTION_DATA_OFFSET] = totalLen - VENDOR_ENTERPRISE_LEN - VENDOR_OPTION_DATA_LEN;

     /* also copy the option code and option len which is not counted in total len */
     memcpy((void*)VIinfo,(void*)optionData,(totalLen+VENDOR_OPTION_DATA_OFFSET));

  return 0;
}

#if 0 /* not used */
/* udp_send and readIp from voice */
static int readIp(const char* ip)
{
   int n = 0;
   int res = 0;
   
   while (n < 4 && *ip)
   {
      if (isdigit(*ip)) 
      {
         res = (res << 8) | atoi(ip);
         n++;
         while (isdigit(*ip)) 
         {
            ip++;
         }
      } 
      else 
      {
         ip++;
		}
   }
   return res;
}

static int notifyApp(short port, void *data, int len)
{
   int sockfd;
   struct sockaddr_in serv_addr;
   struct sockaddr_in cli_addr;
   
   /* fill in server address */
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family      = AF_INET;
   serv_addr.sin_addr.s_addr = readIp("127.0.0.1");
   serv_addr.sin_port        = htons(port);

   /* open udp socket */
   if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
   {
      printf("WT-104: Could not open socket for send\n");
      return -1; /* could not open socket */
   }

   /* bind any local address for us */ 
   memset(&cli_addr, 0, sizeof(cli_addr));
   cli_addr.sin_family      = AF_INET;
   cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   cli_addr.sin_port        = htons(0);

   if (bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0) 
   {
      printf("dhcpd: Could not bind client socket\n");
      return -2; /* could not bind client socket */
   }
 
   /* send the data */
   if (sendto(sockfd, data, len, 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != len) 
   {
      printf( "dhcpd: Could not sendto\n");
      return -3; /* could not sendto */
   }
   close(sockfd);
   return 0;
}
#endif

/* this function parses received VI info, and save it.
   If -1 is return, there is no VI info found; invalid option.  Otherwise, 0 is returned.
   *option is the received option string to parse; it points to optionData.
   lease is internal data where VI info is to be stored. */
int saveVIoption(char *option, struct dhcpOfferedAddr *lease)
{
  char *optionPtr;
#ifdef MTS_Telefonica_TR69	
/* The possible received VI info: OUI, serialNumber, productClass, modelName,
 *  __TELEFONICA__, ZyXEL Eva, 20100323 */
  int maxSubcode = 4;
#else
  int maxSubcode = 3;
#endif
  int parsedLen = 0;
  int subcodeParsed = 0;
  int subcode, sublen;
  int optionLen;
  int ret = 0;
//  int msg = 0;

  if (option == NULL) {
    DEBUG(LOG_ERR, "saveVIoption(): option is NULL.");
    return -1;
  }

  optionPtr = option;
  optionPtr += VENDOR_ENTERPRISE_LEN;
  optionLen = *optionPtr;
  optionPtr +=  VENDOR_OPTION_DATA_LEN;

#ifdef MTS_Telefonica_TR69  /*Telefonica, Jason, 20100611*/	
  memset(lease->oui,0,sizeof(lease->oui));
  memset(lease->serialNumber,0,sizeof(lease->serialNumber));
  memset(lease->productClass,0,sizeof(lease->productClass));
  while (parsedLen < optionLen)
#else
  while ((subcodeParsed < maxSubcode) && (parsedLen <= optionLen)) 
#endif
  {
     /* subcode, len, data */
     subcode = *optionPtr++;
     sublen = *optionPtr++;
     subcodeParsed++;

#ifdef MTS_Telefonica_TR69  /*Telefonica, Jason, 20100611*/	
     parsedLen += (sublen + VENDOR_SUBCODE_AND_LEN_BYTES);
#else
     parsedLen += (sublen + VENDOR_OPTION_SUBCODE_LEN);
#endif

     switch (subcode)
     {
     case VENDOR_DEVICE_OUI_SUBCODE:
     case VENDOR_GATEWAY_OUI_SUBCODE:
        if (sublen <= VENDOR_GATEWAY_OUI_MAX_LEN) 
        {
           memcpy(lease->oui,optionPtr,sublen);
           lease->oui[sublen] = '\0';
        }
#ifdef MTS_Telefonica_TR69
        else if(IS_EMPTY_STRING(lease->oui))
#else
        else 
#endif
		{
           DEBUG(LOG_ERR, "saveVIoption(): subcode OUI, OUI len %d is too long.",sublen);
           goto viError;
        }
        break;
     case VENDOR_DEVICE_SERIAL_NUMBER_SUBCODE:
     case VENDOR_GATEWAY_SERIAL_NUMBER_SUBCODE:
        if (sublen <= VENDOR_GATEWAY_SERIAL_NUMBER_MAX_LEN) 
        {
           memcpy(lease->serialNumber,optionPtr,sublen);
           lease->serialNumber[sublen] = '\0';
        }
#ifdef MTS_Telefonica_TR69
        else if(IS_EMPTY_STRING(lease->serialNumber))
#else
        else 
#endif
        {
           DEBUG(LOG_ERR, "saveVIoption(): subcode SerialNumber, Serial Number len %d is too long.",sublen);
           goto viError;
        }
        break;
     case VENDOR_DEVICE_PRODUCT_CLASS_SUBCODE:
     case VENDOR_GATEWAY_PRODUCT_CLASS_SUBCODE:
        if (sublen <= VENDOR_GATEWAY_PRODUCT_CLASS_MAX_LEN) 
        {
           memcpy(lease->productClass,optionPtr,sublen);
           lease->productClass[sublen] = '\0';
        }
#ifdef MTS_Telefonica_TR69
        else if(IS_EMPTY_STRING(lease->productClass))
#else
        else
#endif
        {
           DEBUG(LOG_ERR, "saveVIoption(): subcode ProductClass, Class len %d is too long.",sublen);
           goto viError;
        }
        break;
#ifdef MTS_Telefonica_TR69	
/* Do nothing when parsing ModelName because it is not included in ManagementServer.ManageableDevice.{i}.,
 *  __TELEFONICA__, ZyXEL Eva, 20100323 */
     case VENDOR_DEVICE_MODEL_NAME_SUBCODE:
     case VENDOR_GATEWAY_MODEL_NAME_SUBCODE:
         if (sublen <= 64) 
         { 		
             break;
         }
         else
         {
             DEBUG(LOG_ERR, "saveVIoption(): subcode ModelName, Model Name len %d is too long.",sublen);
             goto viError;
         }
         break;
#endif
     default:
        DEBUG(LOG_ERR, "saveVIoption(): subcode %d, not supported.",subcode);
        goto viError;
     }
     optionPtr += sublen;
     /* add info to the manageable device link list */
  } /* while subcodeParsed < maxSubcode */

  return ret;

 viError:
  ret = -1;
  if (lease) 
  {
     memset(lease->oui,0,sizeof(lease->oui));
     memset(lease->serialNumber,0,sizeof(lease->serialNumber));
     memset(lease->productClass,0,sizeof(lease->productClass));
  }
  return ret;
}
//brcm end
