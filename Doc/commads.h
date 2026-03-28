
/* FTP命令列表 */
extern cmd_t cmd_list[] = {
    /* 用户类指令 */
    {"ACCT", "Account command (like access right as admin or root), followed by account name", cmd_acct},
    /* 文件类指令 */
    {"CWD", "Change working directory, followed by new working directory path", cmd_cwd},
    {"CDUP", "Change the working directory to parent directory", cmd_cdup},
    {"SMNT", "Mount a different file system data structure, followed by a path", cmd_smnt},
    /* 控制类指令 */
    {"REIN", "Flushing all I/O and account information, but allow any transfer in progress", cmd_rein},
    {"QUIT", "Terminate the user and close the control connection", cmd_quit},
    /* 定义类指令 */
    {"TYPE", "Specifies the representation type", cmd_type},
    {"STRU", "Followed by a single Telnet character code specifying file structure", cmd_stru},
    {"MODE", "Followed by a single Telnet character code specifying the data transfer modes", cmd_mode},
    /* 服务指令 */
    {"RETR", "Causes the server-DTP to transfer a copy of the file,", cmd_retr},
    {"STOR", "Causes the server-DTP to accept the data and to store the data as a file", cmd_stor},
    {"STOU", "Behaves like STOR except that the resultant file is to be created in the current directory under a name unique to that directory", cmd_stou},
    {"APPE", "Causes the server-DTP to store the data in a file. If the file exists, the data shall be appended", cmd_appe},
    {"ALLO", "Used to reserve sufficient storage to accommodate the new file", cmd_allo},
    {"REST", "The argument field represents the server marker at which file transfer is to be restarted", cmd_rest},
    {"RNFR", "Specifies the old pathname of the file to be renamed", cmd_rnfr},
    {"RNTO", "Specifies the new pathname of the file specified in \"RNFR\" command", cmd_rnto},
    {"ABOR", "Abort the previous FTP service command and any associated transfer of data", cmd_abor},
    {"DELE", "Causes the file specified in the pathname to be deleted", cmd_dele},
    {"RMD", "Causes the directory specified in the pathname to be removed", cmd_rmd},
    {"MKD", "Causes the directory specified in the pathname to be created", cmd_mkd},
    {"PWD", "Causes the name of the current working directory to be returned in the reply", cmd_pwd},
    {"SITE", "Used by the server to provide services specific to his system", cmd_site},
    {"STAT", "Cause a status response over the control connection in the form of a reply", cmd_stat},
    {"HELP", "Cause the server to send helpful information over the control connection", cmd_help},
    {"NOOP", "This command does not affect any parameters or previously entered commands", cmd_noop},
    {NULL, NULL, NULL},
};
