#ifndef VSF_DEFS_H
#define VSF_DEFS_H

#ifdef MSTC_TO2_GERMANY_CUSTOMIZATION /*For To2 FTP, MSTC ChaoWen, 20130306*/
#define VSFTP_DEFAULT_CONFIG    "/var/vsftpd.conf"
#else
#define VSFTP_DEFAULT_CONFIG    "/etc/vsftpd.conf"
#endif

#ifdef MSTC_TO2_GERMANY_CUSTOMIZATION /* ChenHe@MSTC for FTP open access, 20130301*/
#define VSFTP_COMMAND_FD        3	/*CMS_DYNAMIC_LAUNCH_SERVER_FD*/
#else
#define VSFTP_COMMAND_FD        0
#endif

#define VSFTP_PASSWORD_MAX      128
#define VSFTP_USERNAME_MAX      128
#define VSFTP_MAX_COMMAND_LINE  4096
#define VSFTP_DATA_BUFSIZE      65536
#define VSFTP_DIR_BUFSIZE       16384
#define VSFTP_PATH_MAX          4096
#define VSFTP_CONF_FILE_MAX     100000
#define VSFTP_LISTEN_BACKLOG    32
#define VSFTP_SECURE_UMASK      077
#define VSFTP_ROOT_UID          0
/* Must be greater than both VSFTP_MAX_COMMAND_LINE and VSFTP_DIR_BUFSIZE */
#define VSFTP_PRIVSOCK_MAXSTR   VSFTP_DATA_BUFSIZE
#define VSFTP_AS_LIMIT          100 * 1024 * 1024

#endif /* VSF_DEFS_H */

