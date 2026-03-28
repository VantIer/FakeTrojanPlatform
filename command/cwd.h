/**
 * @file cwd.h
 * @brief FTP CWD 命令处理头文件
 *
 * 本文件声明处理 FTP 改变工作目录命令的处理函数。
 * CWD 命令允许用户更改当前工作目录。
 */
#ifndef FTP_COMMAND_CWD_H
#define FTP_COMMAND_CWD_H

#include <stddef.h>

/**
 * @brief 处理 FTP 客户端发送的 CWD 命令（改变工作目录）
 *
 * CWD 命令允许用户在不注销的情况下更改当前工作目录。
 * 成功时返回 250 响应，失败时返回相应错误码。
 *
 * @param[in]  argument    CWD 命令参数（新工作目录路径）
 * @param[out] response    用于填充响应消息的缓冲区
 * @param[in]  resp_size   response 缓冲区大小
 * @param[in,out] ctx      指向当前客户端会话上下文
 *
 * @return int
 *         - 0 表示成功处理
 *         - -1 表示内部错误
 */
int handle_ftp_cwd_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx);

#endif /* FTP_COMMAND_CWD_H */