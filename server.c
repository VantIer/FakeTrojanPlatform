/**
 * @file server.c
 * @brief 跨平台 TCP 网络服务器示例
 *
 * 本程序实现一个简单的 TCP 服务器，支持在 Linux 和 Windows 下编译运行。
 * 可监听指定 IP 地址（如 "127.0.0.1" 或 "0.0.0.0"）和端口（如 1996）。
 * 每当有客户端连接时，服务器会创建一个新线程处理该连接。
 * 线程处理函数目前为空，留待后续功能扩展。
 *
 * 编译说明：
 *   - Linux: gcc -o server server.c -lpthread
 *   - Windows (MinGW): gcc -o server.exe server.c -lws2_32
 */
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 跨平台套接字头文件处理
#ifdef _WIN32
#include <process.h> // 用于 _beginthreadex
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

// =====================指令头文件=====================
#include "command/nlst.h"
#include "command/opts.h"
#include "command/pass.h"
#include "command/port.h"
#include "command/pwd.h"
#include "command/syst.h"
#include "command/user.h"
#include "command/pasv.h"
#include "command/list.h"
#include "command/cwd.h"
#include "command/xpwd.h"

// 读取配置文件，初始化
static int read_conf(client_context_t* ctx)
{
    FILE* fp = NULL;
    char line[2048];
    int found_conf = 0;

    // 打开配置文件 config.ini（当前工作目录）
    fp = fopen("config.ini", "r");
    if (!fp) {
        LOG_ERROR("Open config.ini Failed.");
        return -1; // 文件打开失败
    }

    // 逐行读取配置文件
    while (fgets(line, sizeof(line), fp)) {
        // 去除行尾换行符
        line[strcspn(line, "\r\n")] = '\0';

        // 跳过空行或注释行（以 # 开头）
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // 解析 [config]
        if (strncmp(line, "[config]", 8) == 0) {
            // 查找配置项
            found_conf = 1;
        } else if (strncmp(line, "[", 1) == 0) {
            // 没找到
            found_conf = 0;
        } else if (found_conf == 1 && strncmp(line, "path=", 5) == 0) {
            // 找到配置并逐个解析
            const char* value = line + 5;
            strncpy(ctx->base_path, value, sizeof(ctx->base_path) - 1);
            ctx->base_path[sizeof(ctx->base_path) - 1] = '\0';
            if (strlen(ctx->base_path) >= sizeof(ctx->base_path) - 1) {
                LOG_ERROR("Conf Item in config.ini is too big.");
                return -1;
            }
#ifdef _WIN32
            if (ctx->base_path[strlen(ctx->base_path) - 1] != '\\') {
                ctx->base_path[strlen(ctx->base_path)] = '\\';
            }
#else
            if (ctx->base_path[strlen(ctx->base_path) - 1] != '/') {
                ctx->base_path[strlen(ctx->base_path)] = '/';
            }
#endif
            strncpy(ctx->cwd, ctx->base_path, sizeof(ctx->cwd) - 1);
            ctx->cwd[sizeof(ctx->cwd) - 1] = '\0';
        } else if (found_conf == 1 && strncmp(line, "admin_active=1", 14) == 0) {
            // 获取管理员信息
            ctx->admin_active = 1;
        }
    }

    fclose(fp);

    return 0;
}

// 基础指令 - QUIT
int handle_ftp_quit_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验：确保输入参数有效
    if (response == NULL || resp_size == 0) {
        return -1; // 无效参数
    }

    snprintf(response, resp_size, "221 Goodbye.\r\n");

    return 0;
}

// 函数指针类型定义：所有命令处理函数统一
/**
 * @brief FTP 命令处理函数的通用类型
 * @param argument  命令参数（如 USER 后的用户名）
 * @param response  用于写入响应消息的缓冲区
 * @param resp_size response 缓冲区大小
 * @param ctx       FTP 会话上下文指针
 * @return int      0 表示成功，非 0 表示内部错误
 */
typedef int (*ftp_command_handler_t)(const char* argument, char* response, size_t resp_size, client_context_t* ctx);

/**
 * @brief FTP 命令映射表项
 */
typedef struct {
    const char* command; ///< 命令名称（大写）
    ftp_command_handler_t handler; ///< 对应处理函数指针
    int requires_auth; ///< 是否需要认证后才能执行（本例未启用，可扩展）
} ftp_command_entry_t;

static const ftp_command_entry_t ftp_command_table[] = {
    { "CWD",  handle_ftp_cwd_command,  1 },
    { "LIST", handle_ftp_list_command, 1 },
    { "NLST", handle_ftp_nlst_command, 1 },
    { "OPTS", handle_ftp_opts_command, 0 },
    { "PASS", handle_ftp_pass_authentication, 0 },
    { "PASV", handle_ftp_pasv_command, 0 },
    { "PORT", handle_ftp_port_command, 0 },
    { "PWD",  handle_ftp_pwd_command,  0 },
    { "XPWD", handle_ftp_xpwd_command, 0 },
    { "QUIT", handle_ftp_quit_command, 0 },
    { "SYST", handle_ftp_syst_command, 0 },
    { "USER", handle_ftp_user_command, 0 },
    { NULL, NULL, 0 } // 没匹配到任何指令时的终止哨兵
};

/**
 * @brief 客户端连接处理线程函数（实现简易 FTP 控制连接协议）
 * 
 * 本函数模拟一个极简的 FTP 服务器控制连接行为，仅响应基本命令，
 * 不实现数据连接（PORT/PASV）及文件传输功能，但保留可扩展结构。
 * 
 * 支持命令包括：USER, PASS, SYST, PWD, TYPE, QUIT。
 * 所有认证均视为成功（测试用途），实际应用中应加入安全验证逻辑。
 * 
 * @param arg 指向客户端 socket 的指针（需在函数内释放）
 * @return 无实际返回值，仅为兼容线程接口
 */
#ifdef _WIN32
unsigned int __stdcall handle_client(void* arg)
#else
void* handle_client(void* arg)
#endif
{
    // 获取上下文
    client_context_t* ctx = (client_context_t*)arg;
    int client_sock = ctx->sock;
    const char* client_ip = ctx->client_ip;
    unsigned short client_port = ctx->client_port;

    // 发送 FTP 服务就绪欢迎消息（220）
    const char* welcome_msg = WELCOME_MSG;
    send(client_sock, welcome_msg, strlen(welcome_msg), 0);

    // 缓冲区用于接收客户端命令（最大 512 字节，符合 RFC 959 建议）
    char buffer[512];

    // 退出标记
    int quit_flag = 0;

    // 主通信循环
    while (1) {
        // 清空缓冲区
        memset(buffer, 0, sizeof(buffer));

        // 接收客户端命令
        int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            // 客户端断开或出错
            break;
        }

        // 确保字符串以 '\0' 结尾
        buffer[bytes_received] = '\0';

        // 去除末尾的 \r\n（FTP 命令以 CRLF 结尾）
        char* newline = strchr(buffer, '\r');
        if (newline)
            *newline = '\0';
        newline = strchr(buffer, '\n');
        if (newline)
            *newline = '\0';

        // 提取命令和参数
        char command[16] = { 0 };
        char argument[256] = { 0 };
        if (sscanf(buffer, "%15s %255[^\r\n]", command, argument) < 1) {
            // 若无法解析命令，跳过
            continue;
        }

        // 将指令转换为大写以便统一处理（FTP 命令不区分大小写）
        for (char* p = command; *p; ++p) {
            if (*p >= 'a' && *p <= 'z') {
                *p = *p - 'a' + 'A';
            }
        }

        LOG_DEBUG("Client %s:%d, Command is \"%s\", Argument is \"%s\".", client_ip, client_port, command, argument);

        // 构造响应消息
        char response[512] = { 0 };

        // 处理各 FTP 命令
        /*if (strcmp(command, "TYPE") == 0) {
            // 设置传输类型（I=二进制, A=ASCII）（200）
            if (strcmp(argument, "I") == 0 || strcmp(argument, "A") == 0) {
                snprintf(response, sizeof(response), "200 Type set to %s.\r\n", argument);
            } else {
                snprintf(response, sizeof(response), "504 Type not supported.\r\n");
            }
            send(client_sock, response, strlen(response), 0);
        }*/

        // 遍历命令表查找匹配项
        for (int i = 0;; i++) {
            // 检查是否到达表末尾/未识别命令
            if (ftp_command_table[i].command == NULL || ftp_command_table[i].handler == NULL) {
                // 未识别命令
                snprintf(response, sizeof(response), "500 Unknown command '%s'.\r\n", command);
                send(client_sock, response, strlen(response), 0);
                break;
            }

            // 识别指令并调用回调函数
            if (strcmp(command, ftp_command_table[i].command) == 0) {
                // 找到匹配命令，调用对应处理函数
                int ret = ftp_command_table[i].handler(argument, response, sizeof(response), ctx);
                if (ret == 0) {
                    // 成功：发送响应
                    send(client_sock, response, strlen(response), 0);
                } else {
                    // 内部处理失败
                    snprintf(response, sizeof(response), "451 Local Error In Processing.\r\n");
                    send(client_sock, response, strlen(response), 0);
                }

                // 特殊处理 QUIT 命令：设置退出标志
                if (strcmp(command, "QUIT") == 0) {
                    quit_flag = 1;
                    LOG_INFO("Client %s:%d Quit.", client_ip, client_port);
                }

                break;
            }
        }

        // 退出
        if (quit_flag == 1) {
            break;
        }
    }

    // 关闭客户端 socket
#ifdef _WIN32
    free(ctx); // 释放传入的动态分配内存
    closesocket(client_sock);
#else
    free(ctx); // 释放传入的动态分配内存
    close(client_sock);
#endif

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// 定义线程处理函数类型
#ifdef _WIN32
typedef unsigned int(__stdcall* thread_func_t)(void*);
#else
typedef void* (*thread_func_t)(void*);
#endif

// 全局变量：是否初始化 Winsock
static int winsock_initialized = 0;

/**
 * @brief 初始化网络环境（Windows 需要初始化 Winsock）
 * @return 成功返回 0，失败返回非 0
 */
int init_network(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        LOG_ERROR("WSAStartup failed: %d\n", result);
        return -1;
    }
    winsock_initialized = 1;
#endif
    return 0;
}

/**
 * @brief 清理网络环境（Windows 需要清理 Winsock）
 */
void cleanup_network(void)
{
#ifdef _WIN32
    if (winsock_initialized) {
        WSACleanup();
        winsock_initialized = 0;
    }
#endif
}

/**
 * @brief 创建并启动客户端处理线程
 * @param client_sock 已连接的客户端 socket
 * @return 成功返回 0，失败返回 -1
 */
int start_client_thread(int client_sock, const char* client_ip, unsigned short client_port)
{
    // 动态分配内存以传递 socket 值给线程（避免栈变量生命周期问题）
    client_context_t* ctx = (client_context_t*)malloc(sizeof(client_context_t));
    if (!ctx) {
        LOG_ERROR("malloc");
        return -1;
    }

    memset(ctx, 0, sizeof(client_context_t));
    ctx->sock = client_sock;
    ctx->pasv_listen_sock = -1; // 初始化被动模式监听socket
    strncpy(ctx->client_ip, client_ip, INET_ADDRSTRLEN - 1);
    ctx->client_ip[INET_ADDRSTRLEN - 1] = '\0'; // 确保字符串安全终止
    ctx->client_port = client_port;
    ctx->mode = 1;
    ctx->p1 = 11;
    ctx->p2 = 11;
    ctx->port = (ctx->p1 << 8) | ctx->p2;

    // 从 config.ini 读取配置并初始化
    if (read_conf(ctx) != 0) {
        // 读取配置失败
        free(ctx);
        return -1;
    }

#ifdef _WIN32
    // Windows 使用 _beginthreadex 创建线程
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, handle_client, ctx, 0, NULL);
    if (thread == NULL) {
        free(ctx);
        LOG_ERROR("_beginthreadex");
        return -1;
    }
    CloseHandle(thread); // 线程结束后自动清理，无需等待
#else
    // Linux 使用 pthread_create
    pthread_t tid;
    int result = pthread_create(&tid, NULL, handle_client, ctx);
    if (result != 0) {
        free(ctx);
        LOG_ERROR("pthread_create");
        return -1;
    }
    pthread_detach(tid); // 自动回收线程资源
#endif

    return 0;
}

/**
 * @brief 主函数：启动 TCP 服务器
 * @param argc 参数个数
 * @param argv 参数数组，期望格式：./server [ip] [port]
 *             默认 IP: "0.0.0.0"，默认端口: 1996
 * @return 程序退出码
 */
int main(int argc, char* argv[])
{
    const char* ip = SERVER_IP; // 默认监听所有接口
    int port = SERVER_PORT; // 默认端口

    // 解析命令行参数
    if (argc >= 2) {
        ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid port number: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    // 初始化网络环境
    if (init_network() != 0) {
        return EXIT_FAILURE;
    }

    // 创建 TCP socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
#ifdef _WIN32
        LOG_ERROR("socket() failed: %d\n", WSAGetLastError());
#else
        LOG_ERROR("socket");
#endif
        cleanup_network();
        return EXIT_FAILURE;
    }

    // 设置 SO_REUSEADDR（避免 TIME_WAIT 导致端口占用）
    int optval = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) == -1) {
#ifdef _WIN32
        LOG_ERROR("setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
#else
        LOG_ERROR("setsockopt");
#endif
        goto cleanup_and_exit;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid IP address: %s\n", ip);
        goto cleanup_and_exit;
    }

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
#ifdef _WIN32
        LOG_ERROR("bind() failed: %d\n", WSAGetLastError());
#else
        LOG_ERROR("bind");
#endif
        goto cleanup_and_exit;
    }

    // 开始监听
    if (listen(server_sock, SOMAXCONN) == -1) {
#ifdef _WIN32
        LOG_ERROR("listen() failed: %d\n", WSAGetLastError());
#else
        LOG_ERROR("listen");
#endif
        goto cleanup_and_exit;
    }

    printf("Server listening on %s:%d\n", ip, port);

    // 主循环：接受客户端连接
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == -1) {
#ifdef _WIN32
            LOG_ERROR("accept() failed: %d\n", WSAGetLastError());
#else
            LOG_ERROR("accept");
#endif
            continue; // 继续尝试接受下一个连接
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        unsigned short client_port = ntohs(client_addr.sin_port);
        printf("Accepted connection from %s:%d\n", client_ip, client_port);

        // 启动新线程处理客户端
        if (start_client_thread(client_sock, client_ip, client_port) != 0) {
            LOG_ERROR("Failed to start client thread for %s:%d\n", client_ip, client_port);
#ifdef _WIN32
            closesocket(client_sock);
#else
            close(client_sock);
#endif
        }
    }

cleanup_and_exit:
#ifdef _WIN32
    closesocket(server_sock);
#else
    close(server_sock);
#endif
    cleanup_network();
    return EXIT_FAILURE;
}