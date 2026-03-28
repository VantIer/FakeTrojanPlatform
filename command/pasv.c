#include "../utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 跨平台 Socket 头文件处理
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define CLOSE_SOCKET(s) closesocket(s)
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #define CLOSE_SOCKET(s) close(s)
    #define SOCKET_ERROR_CODE errno
#endif

/**
 * @brief 关闭被动模式监听socket
 *
 * @param[in,out] ctx 客户端会话上下文
 */
static void close_pasv_listen_socket(client_context_t* ctx)
{
    if (ctx->pasv_listen_sock != -1) {
        CLOSE_SOCKET(ctx->pasv_listen_sock);
        ctx->pasv_listen_sock = -1;
    }
}

/**
 * @brief 生成随机端口号（用于被动模式）
 *
 * @return unsigned short 随机端口号，范围 1024-65535
 */
static unsigned short generate_random_port(void)
{
    // 使用时间种子生成随机数，确保不同连接有不同端口
    srand((unsigned int)time(NULL) ^ (unsigned int)rand());
    return (unsigned short)((rand() % (65535 - 1024 + 1)) + 1024);
}

/**
 * @brief 获取服务器监听地址（用于227响应）
 *
 * @param[in] ctx 客户端会话上下文
 * @param[out] ip_str IP地址字符串输出缓冲区
 * @param[in] ip_str_size 缓冲区大小
 * @param[out] port 端口号输出指针
 *
 * @return int 成功返回0，失败返回-1
 */
static int get_server_address(client_context_t* ctx, char* ip_str, size_t ip_str_size, unsigned short* port)
{
    // 优先使用控制连接的对端地址（即客户端连接的本地地址）
    // 如果没有，则使用 127.0.0.1
    if (ctx->client_ip[0] != '\0') {
        strncpy(ip_str, ctx->client_ip, ip_str_size - 1);
        ip_str[ip_str_size - 1] = '\0';
    } else {
        strncpy(ip_str, "127.0.0.1", ip_str_size - 1);
        ip_str[ip_str_size - 1] = '\0';
    }

    *port = ctx->pasv_port;
    return 0;
}

int handle_ftp_pasv_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验
    if (response == NULL || resp_size == 0 || ctx == NULL) {
        return -1;
    }

    // 忽略argument参数（PASV命令不带参数）

    // 如果之前已有被动模式监听socket，先关闭
    close_pasv_listen_socket(ctx);

    // 创建新的TCP socket用于被动模式监听
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        LOG_ERROR("Failed to create PASV listen socket.");
        if (snprintf(response, resp_size, "425 Failed to create socket.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 425 error.");
            return -1;
        }
        return 0;
    }

    // 设置 SO_REUSEADDR 选项
    int optval = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) == -1) {
        LOG_WARN("setsockopt(SO_REUSEADDR) failed in PASV.");
    }

    // 如果之前有记录端口，尝试使用；否则生成新的随机端口
    unsigned short pasv_port = generate_random_port();

    // 绑定到指定端口（尝试多次以找到可用端口，最多10次）
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有接口

    int bind_success = 0;
    for (int retry = 0; retry < 10; retry++) {
        listen_addr.sin_port = htons(pasv_port);
        if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == 0) {
            bind_success = 1;
            break;
        }
        // 端口被占用，生成新的随机端口
        pasv_port = generate_random_port();
    }

    if (!bind_success) {
        LOG_ERROR("Failed to bind PASV socket to any available port.");
        CLOSE_SOCKET(listen_sock);
        if (snprintf(response, resp_size, "425 Failed to bind to port.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 425 error.");
            return -1;
        }
        return 0;
    }

    // 开始监听
    if (listen(listen_sock, 1) == -1) {
        LOG_ERROR("Failed to listen on PASV socket.");
        CLOSE_SOCKET(listen_sock);
        if (snprintf(response, resp_size, "425 Failed to listen on port.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 425 error.");
            return -1;
        }
        return 0;
    }

    // 保存监听socket和端口到上下文
    ctx->pasv_listen_sock = listen_sock;
    ctx->pasv_port = pasv_port;
    ctx->mode = 1;  // 1表示被动模式

    // 获取用于227响应的IP地址
    char ip_str[16];
    if (ctx->client_ip[0] != '\0') {
        strncpy(ip_str, ctx->client_ip, sizeof(ip_str) - 1);
        ip_str[sizeof(ip_str) - 1] = '\0';
    } else {
        // 回退到127.0.0.1
        strncpy(ip_str, "127.0.0.1", sizeof(ip_str) - 1);
        ip_str[sizeof(ip_str) - 1] = '\0';
    }

    // 解析IP地址为四个十进制数
    unsigned int ip_parts[4];
    if (sscanf(ip_str, "%u.%u.%u.%u", &ip_parts[0], &ip_parts[1], &ip_parts[2], &ip_parts[3]) != 4) {
        LOG_ERROR("Failed to parse client IP: %s", ip_str);
        close_pasv_listen_socket(ctx);
        if (snprintf(response, resp_size, "425 Internal error.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 425 error.");
            return -1;
        }
        return 0;
    }

    // 计算端口号的高位和低位字节
    unsigned short p1 = (pasv_port >> 8) & 0xFF;  // 端口号高字节
    unsigned short p2 = pasv_port & 0xFF;          // 端口号低字节

    // 发送227响应
    int ret = snprintf(response, resp_size,
        "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
        ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3],
        p1, p2);

    if (ret < 0 || (size_t)ret >= resp_size) {
        LOG_ERROR("Response buffer too small for 227 message.");
        close_pasv_listen_socket(ctx);
        return -1;
    }

    LOG_INFO("PASV mode enabled on %s:%u, socket=%d", ip_str, pasv_port, listen_sock);
    return 0;
}

int accept_ftp_data_connection(client_context_t* ctx, int* data_sock, int timeout_sec)
{
    if (ctx == NULL || data_sock == NULL) {
        LOG_ERROR("Invalid parameters for accept_ftp_data_connection.");
        return -1;
    }

    *data_sock = -1;

    if (ctx->pasv_listen_sock == -1) {
        LOG_ERROR("PASV listen socket not initialized.");
        return -1;
    }

    // 设置接受超时
#ifdef _WIN32
    DWORD timeout_ms = (DWORD)(timeout_sec * 1000);
    if (setsockopt(ctx->pasv_listen_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
        LOG_WARN("setsockopt SO_RCVTIMEO failed.");
    }
#else
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    if (setsockopt(ctx->pasv_listen_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        LOG_WARN("setsockopt SO_RCVTIMEO failed.");
    }
#endif

    // 接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    *data_sock = accept(ctx->pasv_listen_sock, (struct sockaddr*)&client_addr, &client_len);

    if (*data_sock == -1) {
#ifdef _WIN32
        LOG_ERROR("Accept failed on PASV socket (error=%d).", WSAGetLastError());
#else
        LOG_ERROR("Accept failed on PASV socket (errno=%d).", errno);
#endif
        return -1;
    }

    // 记录连接信息
    char client_ip[INET_ADDRSTRLEN];
    unsigned short client_port = 0;

    if (inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, sizeof(client_ip)) != NULL) {
        client_port = ntohs(client_addr.sin_port);
        LOG_INFO("Data connection accepted from %s:%d", client_ip, client_port);
    }

    // 关闭监听socket（被动模式只需要一次连接）
    close_pasv_listen_socket(ctx);

    return 0;
}