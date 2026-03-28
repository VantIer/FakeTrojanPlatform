#include "../utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pasv.h"

// 跨平台 Socket 头文件处理
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <direct.h>
    #define CLOSE_SOCKET(s) closesocket(s)
    #define STRDUP _strdup
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define CLOSE_SOCKET(s) close(s)
    #define STRDUP strdup
#endif

/**
 * @brief 处理 FTP 客户端发送的 NLST 命令（简略文件列表）
 *
 * 本函数实现 FTP 的 NLST 命令，用于向客户端返回指定目录下所有文件和子目录的名称列表。
 * 数据连接支持主动模式（PORT）和被动模式（PASV）。
 *
 * 功能流程：
 *   1. 检查是否已通过 PORT 或 PASV 命令设置数据连接；
 *   2. 解析可选路径参数，若无则使用当前工作目录（ctx->cwd）；
 *   3. 主动模式：连接到 ctx->data_ip:ctx->data_port；
 *      被动模式：接受来自 PASV 监听 socket 的连接；
 *   4. 打开目录并遍历所有条目；
 *   5. 将每个条目名称通过数据连接发送（格式：name\r\n）；
 *   6. 关闭数据连接；
 *   7. 在控制连接上返回 150 和 226 响应。
 *
 * @param[in]  argument    指向 NLST 命令后的可选路径参数（可能为 NULL 或空字符串）
 * @param[out] response    用于填充最终控制连接响应消息的缓冲区
 * @param[in]  resp_size   response 缓冲区大小
 * @param[in,out] ctx      指向客户端会话上下文
 *
 * @return int
 *         - 0 表示成功处理
 *         - -1 表示内部错误
 */
int handle_ftp_nlst_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验
    if (response == NULL || resp_size == 0 || ctx == NULL) {
        return -1;
    }

    // 检查是否已设置 PORT（主动模式 mode=0）或 PASV（被动模式 mode=1）
    if (ctx->mode == 0) {
        // 主动模式：需要验证 PORT 是否已设置
        if (ctx->data_ip[0] == '\0' || ctx->port == 0) {
            if (snprintf(response, resp_size, "425 Use PORT or PASV first.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }
    } else if (ctx->mode == 1) {
        // 被动模式：需要验证 PASV 是否已设置
        if (ctx->pasv_listen_sock == -1) {
            if (snprintf(response, resp_size, "425 Use PORT or PASV first.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }
    } else {
        if (snprintf(response, resp_size, "425 Use PORT or PASV first.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 425 error.");
            return -1;
        }
        return 0;
    }

    // 构建目标路径
    char target_path[2048] = {0};
    if (argument == NULL || strlen(argument) == 0) {
        strncpy(target_path, ctx->cwd, sizeof(target_path) - 1);
    } else {
        const char* start = argument;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0') {
            strncpy(target_path, ctx->cwd, sizeof(target_path) - 1);
        } else {
            const char* end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
            size_t len = end - start + 1;
            if (len >= sizeof(target_path)) {
                LOG_INFO("Path too long in NLST from %s:%d.", ctx->client_ip, ctx->client_port);
                if (snprintf(response, resp_size, "550 Path too long.\r\n") >= (int)resp_size) {
                    LOG_ERROR("Response buffer too small for path error.");
                    return -1;
                }
                return 0;
            }
            strncpy(target_path, start, len);
            target_path[len] = '\0';
        }
    }

    // 处理相对路径
    char full_path[2048] = {0};
    // 处理"/"开通的linux绝对路径，以及win下类似于"C:\"、"AZ:\"之类的路径
    if (target_path[0] == '/' || (strlen(target_path) >= 2 && target_path[1] == ':') || (strlen(target_path) >= 3 && target_path[2] == ':')) {
        strncpy(full_path, target_path, sizeof(full_path) - 1);
    } else {
        // 相对路径：拼接到当前工作目录
        if (strlen(ctx->cwd) + 1 + strlen(target_path) >= sizeof(full_path)) {
            LOG_INFO("Path too long in NLST from %s:%d.", ctx->client_ip, ctx->client_port);
            if (snprintf(response, resp_size, "550 Path too long.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for path error.");
                return -1;
            }
            return 0;
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", ctx->cwd, target_path);
    }

    // 路径规范化与安全校验
    memset(target_path, 0, sizeof(target_path));
#ifdef _WIN32
    if (_fullpath(target_path, full_path, sizeof(target_path)) == NULL) {
        LOG_INFO("Invalid path in NLST: %s from %s:%d.", full_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Invalid path.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.");
            return -1;
        }
        return 0;
    }

    // Windows 下路径分隔符规范化
    for (char* p = ctx->base_path; *p; ++p) {
        if (*p == '/') *p = '\\';
    }

    if (strncmp(target_path, ctx->base_path, strlen(ctx->base_path)) != 0) {
        LOG_INFO("Access denied in NLST: %s from %s:%d.", target_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Access denied: Path outside allowed root.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for security error.");
            return -1;
        }
        return 0;
    }
#else
    if (realpath(full_path, target_path) == NULL) {
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path + strlen(ctx->base_path)) >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.");
            return -1;
        }
        return 0;
    }

    // Linux 下路径分隔符规范化
    for (char* p = ctx->base_path; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

    if (strncmp(target_path, ctx->base_path, strlen(ctx->base_path)) != 0) {
        LOG_INFO("Access denied in NLST: %s from %s:%d.", target_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Access denied: Path outside allowed root.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for security error.");
            return -1;
        }
        return 0;
    }
#endif

    // 检查目录是否存在
#ifdef _WIN32
    DWORD dwAttrib = GetFileAttributesA(target_path);
    if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path + strlen(ctx->base_path)) >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.");
            return -1;
        }
        return 0;
    }
#else
    DIR* dir = opendir(target_path);
    if (dir == NULL) {
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path + strlen(ctx->base_path)) >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.");
            return -1;
        }
        return 0;
    }
#endif

    // 建立数据连接
    int data_sock = -1;

    if (ctx->mode == 0) {
        // 主动模式：连接到客户端指定的数据端口
        data_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (data_sock == -1) {
            LOG_ERROR("Failed to create data socket.");
            if (snprintf(response, resp_size, "425 Failed to create data connection.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }

        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(ctx->port);
        if (inet_pton(AF_INET, ctx->data_ip, &client_addr.sin_addr) <= 0) {
            LOG_ERROR("Invalid client data IP: %s", ctx->data_ip);
            CLOSE_SOCKET(data_sock);
            if (snprintf(response, resp_size, "425 Invalid data connection address.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }

        if (connect(data_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
            LOG_ERROR("Failed to connect to client data port %s:%d.", ctx->data_ip, ctx->port);
            CLOSE_SOCKET(data_sock);
            if (snprintf(response, resp_size, "425 Cannot build data connection.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }
    } else {
        // 被动模式：接受客户端的连接
        if (accept_ftp_data_connection(ctx, &data_sock, 60) != 0) {
            LOG_ERROR("Failed to accept data connection from %s:%d.", ctx->client_ip, ctx->client_port);
            if (snprintf(response, resp_size, "425 Cannot build data connection.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for 425 error.");
                return -1;
            }
            return 0;
        }
    }

    // 发送 150 响应
    if (snprintf(response, resp_size, "150 Opening ASCII mode data connection for file list.\r\n") >= (int)resp_size) {
        LOG_ERROR("Response buffer too small for 150 message.");
        CLOSE_SOCKET(data_sock);
        return -1;
    }
    send(ctx->sock, response, strlen(response), 0);

    // 发送文件列表
#ifdef _WIN32
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", target_path);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const char* name = find_data.cFileName;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }
            char line[1024];
            int len = snprintf(line, sizeof(line), "%s\r\n", name);
            if (send(data_sock, line, len, 0) < len) {
                LOG_WARN("Partial send on data connection.");
                break;
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
#else
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        char line[1024];
        int len = snprintf(line, sizeof(line), "%s\r\n", name);
        if (send(data_sock, line, len, 0) < len) {
            LOG_WARN("Partial send on data connection.");
            break;
        }
    }
    closedir(dir);
#endif

    // 关闭数据连接
    CLOSE_SOCKET(data_sock);

    // 发送 226 响应
    if (snprintf(response, resp_size, "226 Transfer complete.\r\n") >= (int)resp_size) {
        LOG_ERROR("Response buffer too small for 226 message.");
        return -1;
    }

    return 0;
}
