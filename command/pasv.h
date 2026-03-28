/**
 * @file pasv.h
 * @brief FTP PASV 命令处理头文件
 *
 * 本文件声明处理 FTP 被动模式连接的命令处理函数。
 * PASV 命令要求服务器在指定端口上监听，等待客户端发起数据连接。
 */

#ifndef FTP_COMMAND_PASV_H
#define FTP_COMMAND_PASV_H

#include <stddef.h>

/**
 * @brief 处理 FTP 客户端发送的 PASV 命令（被动模式）
 *
 * PASV 命令请求服务器在某个端口上监听，等待客户端连接用于数据通信。
 * 服务器响应 227 消息，包含 IP 地址和端口号供客户端连接。
 *
 * 实现流程：
 *   1. 创建 TCP socket 并绑定到任意可用端口
 *   2. 设置为监听模式
 *   3. 从中提取 IP 地址和端口号（格式：h1,h2,h3,h4,p1,p2）
 *   4. 将监听 socket 保存到 ctx->pasv_listen_sock
 *   5. 将端口保存到 ctx->pasv_port
 *   6. 切换 ctx->mode = 1（被动模式）
 *   7. 发送 227 响应给客户端
 *
 * 注意：
 *   - 服务器 IP 地址使用控制连接的本地地址
 *   - 若端口分配失败或 socket 创建失败，返回 425 错误
 *   - 每次 PASV 调用会关闭之前的被动模式监听 socket（如果存在）
 *   - 数据连接建立后，需调用 accept_ftp_data_connection() 接受连接
 *
 * @param[in]  argument    PASV 命令参数（通常为 NULL 或空）
 * @param[out] response    用于填充响应消息的缓冲区
 * @param[in]  resp_size   response 缓冲区大小，防止溢出
 * @param[in,out] ctx      指向当前客户端会话上下文
 *
 * @return int
 *         - 0 表示成功处理
 *         - -1 表示内部错误（如 socket 创建失败、内存问题等）
 */
int handle_ftp_pasv_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx);

/**
 * @brief 接受被动模式数据连接
 *
 * 当客户端连接到 PASV 指定的端口后，调用此函数接受连接。
 * 该函数会阻塞直到有客户端连接或超时。
 *
 * @param[in,out] ctx      指向当前客户端会话上下文，包含 pasv_listen_sock
 * @param[out] data_sock  接受后的数据连接 socket
 * @param[in] timeout_sec 超时时间（秒）
 *
 * @return int
 *         - 0 表示成功接受连接
 *         - -1 表示错误（超时、socket 错误等）
 */
int accept_ftp_data_connection(client_context_t* ctx, int* data_sock, int timeout_sec);

#endif /* FTP_COMMAND_PASV_H */