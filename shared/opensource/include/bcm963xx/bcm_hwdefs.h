/*
<:label-BRCM:2012:DUAL/GPL:standard 

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2, as published by
the Free Software Foundation (the "GPL").

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.


A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.

:>
*/                       

/***********************************************************************/
/*                                                                     */
/*   MODULE:  bcm_hwdefs.h                                             */
/*   PURPOSE: Define all base device addresses and HW specific macros. */
/*                                                                     */
/***********************************************************************/
#ifndef _BCM_HWDEFS_H
#define _BCM_HWDEFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define	DYING_GASP_API

/*****************************************************************************/
/*                    Physical Memory Map                                    */
/*****************************************************************************/

#define PHYS_DRAM_BASE          0x00000000      /* Dynamic RAM Base */
#if defined(CONFIG_BRCM_IKOS)
#define PHYS_FLASH_BASE         0x18000000      /* Flash Memory     */
#else
#define PHYS_FLASH_BASE         0x1FC00000      /* Flash Memory     */
#endif

/*****************************************************************************/
/* Note that the addresses above are physical addresses and that programs    */
/* have to use converted addresses defined below:                            */
/*****************************************************************************/
#define DRAM_BASE           (0x80000000 | PHYS_DRAM_BASE)   /* cached DRAM */
#define DRAM_BASE_NOCACHE   (0xA0000000 | PHYS_DRAM_BASE)   /* uncached DRAM */

/* Binary images are always built for a standard MIPS boot address */
#define IMAGE_BASE          (0xA0000000 | PHYS_FLASH_BASE)

/* Some chips don't support alternative boot vector */
#if defined(CONFIG_BRCM_IKOS)
#define FLASH_BASE          (0xA0000000 | PHYS_FLASH_BASE)  /* uncached Flash  */
#define BOOT_OFFSET         0
#else
#if defined(_BCM96328_) || defined(CONFIG_BCM96328) || defined(_BCM96362_) || defined(CONFIG_BCM96362) || defined(_BCM963268_) || defined(CONFIG_BCM963268) || defined(_BCM96828_) || defined(CONFIG_BCM96828) || defined(_BCM96318_) || defined(CONFIG_BCM96318) 
#define FLASH_BASE          0xB8000000
#else
#define FLASH_BASE          (0xA0000000 | (MPI->cs[0].base & 0xFFFFFF00))
#endif
#define BOOT_OFFSET         (FLASH_BASE - IMAGE_BASE)
#endif

/*****************************************************************************/
/*  Select the PLL value to get the desired CPU clock frequency.             */
/*****************************************************************************/
#define FPERIPH            50000000

/*****************************************************************************/
/* Board memory type offset                                                  */
/*****************************************************************************/
#define ONEK                            1024
/*__VERIZON__, Autumn*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define FLASH_LENGTH_BOOT_ROM           (128*ONEK)
#define FLASH_IMAGE_DOWNLOAD_SIZE       (40*ONEK*ONEK)
#else /*MTS_NAND_FLASH_SUPPORT*/
#define FLASH_LENGTH_BOOT_ROM           (64*ONEK)
#endif /*MTS_NAND_FLASH_SUPPORT*/
/*****************************************************************************/
/*       NVRAM Offset and definition                                         */
/*****************************************************************************/
#define NVRAM_SECTOR                    0
#define NVRAM_DATA_OFFSET               0x0580
#define NVRAM_DATA_ID                   0x0f1e2d3c  // This is only for backwards compatability

#define NVRAM_LENGTH                    ONEK    // 1k nvram 
#define NVRAM_VERSION_NUMBER            6
#define NVRAM_FULL_LEN_VERSION_NUMBER   5 /* version in which the checksum was
                                             placed at the end of the NVRAM
                                             structure */

#define NVRAM_BOOTLINE_LEN              256
#define NVRAM_BOARD_ID_STRING_LEN       16
#define NVRAM_MAC_ADDRESS_LEN           6

#define NVRAM_GPON_SERIAL_NUMBER_LEN    13
#define NVRAM_GPON_PASSWORD_LEN         11


#define NVRAM_WLAN_PARAMS_LEN      256
#define NVRAM_WPS_DEVICE_PIN_LEN   8

#define THREAD_NUM_ADDRESS_OFFSET       (NVRAM_DATA_OFFSET + 4 + NVRAM_BOOTLINE_LEN + NVRAM_BOARD_ID_STRING_LEN)
#define THREAD_NUM_ADDRESS              (0x80000000 + THREAD_NUM_ADDRESS_OFFSET)

#define DEFAULT_BOOTLINE    "e=192.168.1.1:ffffff00 h=192.168.1.100 g= r=f f=vmlinux i=bcm963xx_fs_kernel d=1 p=0 "
#define DEFAULT_BOARD_IP    "192.168.1.1"
#define DEFAULT_MAC_NUM     10
#define DEFAULT_BOARD_MAC   "00:10:18:00:00:00"
#define DEFAULT_TP_NUM      0

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define DEFAULT_PSI_SIZE    128
#else /*MTS_NAND_FLASH_SUPPORT*/
#define DEFAULT_PSI_SIZE    24
#endif /*MTS_NAND_FLASH_SUPPORT*/

#define DEFAULT_GPON_SN     "BRCM12345678"
#define DEFAULT_GPON_PW     "          "

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define DEFAULT_FLASHBLK_SIZE  64
#define MAX_FLASHBLK_SIZE      128
#define DEFAULT_AUXFS_PERCENT 0
#define MAX_AUXFS_PERCENT   80
#define DEFAUT_BACKUP_PSI  1     //jchang change to 1
#endif /*MTS_NAND_FLASH_SUPPORT*/

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define NVRAM_VENDORNAME_LEN        32
#define NVRAM_PRODUCTNAME_LEN       32
#define NVRAM_BUILDINFO_LEN         10
#define NVRAM_FEATUREBITS_LEN       30
#define NVRAM_SERIALNUMBER_LEN      14
#if 1 /* ChenHe@MSTC for TO2_Germany WPA-Key, 20130402 */
#define DEFAULT_WPAKEY	         "0123456789012345"
#define NVRAM_WPAKEY_LEN			17
#define DEFAULT_DECTRFPI		{0x00, 0x00, 0x00, 0x00, 0x00}
#define DEFAULT_DECTRXTUN		0x00
#define DEFAULT_DECTFULLPOWER	0x00
#define DEFAULT_DECTLOWPOWER	0x53
#define NVRAM_DECT_RFPI_SIZE 5
#define NVRAM_DECT_RXTUN_SIZE 1
#define NVRAM_DECT_LOWPOWER_SIZE 1
#define NVRAM_DECT_FULLPOWER_SIZE 1

#endif
#define DEFAULT_VENDORNAME       "MitraStar Technology Corp."
#define DEFAULT_PRODUCTNAME      "HomeBox2"	
#define DEFAULT_SERIALNUMBER     "0000000000000"

#define DEFAULT_DEBUGFLAG        1
#define DEFAULT_FEATUREBITS0     "ZY"
#define DEFAULT_FEATUREBITS2     0x00
#define DEFAULT_FEATUREBITS3     0x0e
#define DEFAULT_MAINFEATUREBIT   0

// __MSTC__, zongyue: reduce manufacture bootup time for wireless calibration
#define ATMT_FEATUREBITS_IDX	 4

/*Merge from P880 trunk : implement ATMC,  __TELEFONICA__,  JhihShan, 20120419.*/
#define ATMC_FEATUREBITS_IDX     (7)
#endif /*MTS_NAND_FLASH_SUPPORT*/

#define DEFAULT_WPS_DEVICE_PIN     "12345670"

#define DEFAULT_VOICE_BOARD_ID     "NONE"

#define NVRAM_MAC_COUNT_MAX         32

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define NVRAM_MAX_PSI_SIZE  128
#else
#define NVRAM_MAX_PSI_SIZE          64
#endif
#define NVRAM_MAX_SYSLOG_SIZE       256

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
/* Add firmware upgrade partition */
#define NP_BOOT             0
#define NP_ROOTFS_1         1
#define NP_ROOTFS_2         2
#define NP_DATA             3
#define NP_FWUPGRADE        4
#define NP_BBT              5
#define NP_TOTAL            6
#else /*MTS_NAND_FLASH_SUPPORT*/
#define NP_BOOT             0
#define NP_ROOTFS_1         1
#define NP_ROOTFS_2         2
#define NP_DATA             3
#define NP_BBT              4
#define NP_TOTAL            5
#endif /*MTS_NAND_FLASH_SUPPORT*/

#define NAND_DATA_SIZE_KB       4096
#define NAND_BBT_THRESHOLD_KB   (512 * 1024)
#define NAND_BBT_SMALL_SIZE_KB  1024
#define NAND_BBT_BIG_SIZE_KB    4096

#define NAND_CFE_RAM_NAME           "cferam.000"
#define NAND_RFS_OFS_NAME           "NAND_RFS_OFS"
#define NAND_COMMAND_NAME           "NANDCMD"
#define NAND_BOOT_STATE_FILE_NAME   "boot_state_x"
#define NAND_SEQ_MAGIC              0x53510000

#define NAND_FULL_PARTITION_SEARCH  0

#if (NAND_FULL_PARTITION_SEARCH == 1)
#define MAX_NOT_JFFS2_VALUE         0 /* infinite */
#else
#define MAX_NOT_JFFS2_VALUE         10
#endif

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
/* Add firmware upgrade partition */
#define IMAGE_FILE_DIR			"/firmware/fw/upload"
#define IMAGE_FILE_NAME			"ras.bin"
#define FW_TMP_DIR			"/firmware/fw"
#endif /*MTS_NAND_FLASH_SUPPORT*/

#ifndef _LANGUAGE_ASSEMBLY

#if 1 /* Amanda@MSTC for TO2_Germany DECT parameters, 20131231 */
#define NVRAM_DECT_RFPI_SIZE 5
#define NVRAM_DECT_RXTUN_SIZE 1
#define NVRAM_DECT_LOWPOWER_SIZE 1
#define NVRAM_DECT_FULLPOWER_SIZE 1
#define NVRAM_DECT_TOTAL_SIZE (sizeof(MrdDect))

typedef struct s_MrdDect
{
  unsigned char rfpi[NVRAM_DECT_RFPI_SIZE];
  unsigned char rxtun[NVRAM_DECT_RXTUN_SIZE];
  unsigned char lowPower[NVRAM_DECT_LOWPOWER_SIZE];
  unsigned char fullPower[NVRAM_DECT_FULLPOWER_SIZE];
}MrdDect;
#endif

typedef struct
{
    unsigned long ulVersion;
    char szBootline[NVRAM_BOOTLINE_LEN];
    char szBoardId[NVRAM_BOARD_ID_STRING_LEN];
    unsigned long ulMainTpNum;
    unsigned long ulPsiSize;
    unsigned long ulNumMacAddrs;
    unsigned char ucaBaseMacAddr[NVRAM_MAC_ADDRESS_LEN];
    char pad;
    char backupPsi;  /**< if 0x01, allocate space for a backup PSI */
    unsigned long ulCheckSumV4;
    char gponSerialNumber[NVRAM_GPON_SERIAL_NUMBER_LEN];
    char gponPassword[NVRAM_GPON_PASSWORD_LEN];
    char wpsDevicePin[NVRAM_WPS_DEVICE_PIN_LEN];
    char wlanParams[NVRAM_WLAN_PARAMS_LEN];
    unsigned long ulSyslogSize; /**< number of KB to allocate for persistent syslog */
    unsigned long ulNandPartOfsKb[NP_TOTAL];
    unsigned long ulNandPartSizeKb[NP_TOTAL];
    char szVoiceBoardId[NVRAM_BOARD_ID_STRING_LEN];
    unsigned long afeId[2];
/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
    unsigned short opticRxPwrReading;   // optical initial rx power reading
    unsigned short opticRxPwrOffset;    // optical rx power offset
    unsigned short opticTxPwrReading;   // optical initial tx power reading
    unsigned long ulBoardStuffOption;   // board options. bit0-3 is for DECT
    /* __MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11 */
    char VendorName[NVRAM_VENDORNAME_LEN];
    char ProductName[NVRAM_PRODUCTNAME_LEN];
    char FeatureBits[NVRAM_FEATUREBITS_LEN];
    char BuildInfo[NVRAM_BUILDINFO_LEN];
    char SerialNumber[NVRAM_SERIALNUMBER_LEN];
    char FeatureBit;
    char EngDebugFlag;
    char countryCode;
    /*__VERIZON__, ALLEN*/
    unsigned char ucFlashBlkSize;
    unsigned char ucAuxFSPercent;
    /*MitraStar, Elina*/
    char updateConfig;
#if 1 /* ChenHe@MSTC for TO2_Germany WPA-Key, 20130402 */
    char WPAKey[NVRAM_WPAKEY_LEN];
    MrdDect Dect;    // Amanda@MSTC for TO2_Germany DECT parameters, 20131231
    char chUnused[151 - NVRAM_DECT_TOTAL_SIZE];
#else
    /*Add firmware upgrade partition*/
    char chUnused[168];
#endif
#else /*MTS_NAND_FLASH_SUPPORT*/
    char chUnused[350];
#endif /*MTS_NAND_FLASH_SUPPORT*/
    unsigned long ulCheckSum;
} NVRAM_DATA, *PNVRAM_DATA;
#endif

/*__MTS__, RaynorChung: Support 963268 nand flash, patch form SVN#3597 on http://svn.zyxel.com.tw/svn/CPE_SW1/BCM96368/trunk/P-870HA/branches/cht/fttb8/4.11*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define BOOT_LATEST_IMAGE   '0'
#define BOOT_PREVIOUS_IMAGE '1'
#endif
/*****************************************************************************/
/*       Misc Offsets                                                        */
/*****************************************************************************/
#define CFE_VERSION_OFFSET           0x0570
#define CFE_VERSION_MARK_SIZE        5
#define CFE_VERSION_SIZE             5

/*****************************************************************************/
/*       Scratch Pad Defines                                                 */
/*****************************************************************************/
/* SP - Persistent Scratch Pad format:
       sp header        : 32 bytes
       tokenId-1        : 8 bytes
       tokenId-1 len    : 4 bytes
       tokenId-1 data    
       ....
       tokenId-n        : 8 bytes
       tokenId-n len    : 4 bytes
       tokenId-n data    
*/

#define MAGIC_NUM_LEN       8
#define MAGIC_NUMBER        "gOGoBrCm"
#define TOKEN_NAME_LEN      16
#define SP_VERSION          1
/*ZyXEL, Autumn*/
#ifdef MTS_NAND_FLASH_SUPPORT
#define SP_RESERVERD        20
#else
#define CFE_NVRAM_DATA2_LEN 20
#endif /*MTS_NAND_FLASH_SUPPORT*/

#ifndef _LANGUAGE_ASSEMBLY
typedef struct _SP_HEADER
{
    char SPMagicNum[MAGIC_NUM_LEN];             // 8 bytes of magic number
    int SPVersion;                              // version number
/*ZyXEL, Autumn*/
#ifdef MTS_NAND_FLASH_SUPPORT
    char SPReserved[SP_RESERVERD];              // reservied, total 32 bytes
#else
    char NvramData2[CFE_NVRAM_DATA2_LEN];       // not related to scratch pad
                                           // additional NVRAM_DATA
#endif /*MTS_NAND_FLASH_SUPPORT*/
} SP_HEADER, *PSP_HEADER;                       // total 32 bytes

typedef struct _TOKEN_DEF
{
    char tokenName[TOKEN_NAME_LEN];
    int tokenLen;
} SP_TOKEN, *PSP_TOKEN;
#endif /*_LANGUAGE_ASSEMBLY*/

/*****************************************************************************/
/*       Boot Loader Parameters                                              */
/*****************************************************************************/

#define BLPARMS_MAGIC               0x424c504d

#define BOOTED_IMAGE_ID_NAME        "boot_image"

#define BOOTED_NEW_IMAGE            1
#define BOOTED_OLD_IMAGE            2
#define BOOTED_ONLY_IMAGE           3
#define BOOTED_PART1_IMAGE          4
#define BOOTED_PART2_IMAGE          5

#define BOOT_SET_NEW_IMAGE          '0'
#define BOOT_SET_OLD_IMAGE          '1'
#define BOOT_SET_NEW_IMAGE_ONCE     '2'
#define BOOT_GET_IMAGE_VERSION      '3'
#define BOOT_GET_BOOTED_IMAGE_ID    '4'
#define BOOT_SET_PART1_IMAGE        '5'
#define BOOT_SET_PART2_IMAGE        '6'
#define BOOT_SET_PART1_IMAGE_ONCE   '7'
#define BOOT_SET_PART2_IMAGE_ONCE   '8'
#define BOOT_GET_BOOT_IMAGE_STATE   '9'

#define FLASH_PARTDEFAULT_REBOOT    0x00000000
#define FLASH_PARTDEFAULT_NO_REBOOT 0x00000001
#define FLASH_PART1_REBOOT          0x00010000
#define FLASH_PART1_NO_REBOOT       0x00010001
#define FLASH_PART2_REBOOT          0x00020000
#define FLASH_PART2_NO_REBOOT       0x00020001

#define FLASH_IS_NO_REBOOT(X)       ((X) & 0x0000ffff)
#define FLASH_GET_PARTITION(X)      ((unsigned long) (X) >> 16)

/*****************************************************************************/
/*       Global Shared Parameters                                            */
/*****************************************************************************/

#define BRCM_MAX_CHIP_NAME_LEN	16


#ifdef __cplusplus
}
#endif

#endif /* _BCM_HWDEFS_H */

