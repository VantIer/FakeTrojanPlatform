/**
 * @file xpwd.h
 * @brief FTP XPWD 命令处理头文件
 *
 * 本文件声明处理 FTP 物理工作目录命令的处理函数。
 * XPWD 命令返回当前工作目录的物理路径（Physical Working Directory）。
 */
#ifndef FTP_COMMAND_XPWD_H
#define FTP_COMMAND_XPWD_H

#include <stddef.h>

/**
 * @brief 处理 FTP 客户端发送的 XPWD 命令（物理工作目录）
 *
 * XPWD 命令返回当前工作目录的物理路径。
 * 与 PWD 命令功能相同，返回服务器的物理文件系统路径。
 * 根据 FTP 协议标准，返回格式为 "257 <directory path>"。
 *
 * @param[in]  argument    XPWD 命令参数（通常为空）
 * @param[out] response    用于填充响应消息的缓冲区
 * @param[in]  resp_size   response 缓冲区大小
 * @param[in,out] ctx      指向当前客户端会话上下文
 *
 * @return int
 *         - 0 表示成功处理
 *         - -1 表示内部错误
 */
int handle_ftp_xpwd_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx);

#endif /* FTP_COMMAND_XPWD_H */