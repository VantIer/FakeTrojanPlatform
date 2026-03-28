/**
 * @file list.h
 * @brief FTP LIST 命令处理头文件
 */
#ifndef FTP_COMMAND_LIST_H
#define FTP_COMMAND_LIST_H

#include <stddef.h>

/**
 * @brief 处理 FTP LIST 命令（详细文件列表）
 *
 * 支持主动模式(PORT)和被动模式(PASV)数据连接。
 * 返回目录的详细信息，包括权限、大小、修改日期和名称。
 */
int handle_ftp_list_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx);

#endif /* FTP_COMMAND_LIST_H */