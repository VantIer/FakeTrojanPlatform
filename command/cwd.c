/**
 * @file cwd.c
 * @brief FTP CWD 命令实现
 */
#include "../utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <direct.h>
    #include <sys/stat.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

int handle_ftp_cwd_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验
    if (response == NULL || resp_size == 0 || ctx == NULL) {
        return -1;
    }

    // CWD 命令必须带有参数
    if (argument == NULL || strlen(argument) == 0) {
        snprintf(response, resp_size, "501 Syntax error in parameters.\r\n");
        return 0;
    }

    // 解析目标路径
    char target_path[2048] = {0};
    const char* start = argument;
    while (*start == ' ' || *start == '\t') start++;

    if (*start == '\0') {
        snprintf(response, resp_size, "501 Argument required.\r\n");
        return 0;
    }

    const char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    size_t len = end - start + 1;

    if (len >= sizeof(target_path)) {
        LOG_INFO("Path too long in CWD from %s:%d.\n", ctx->client_ip, ctx->client_port);
        snprintf(response, resp_size, "550 Path too long.\r\n");
        return 0;
    }

    strncpy(target_path, start, len);
    target_path[len] = '\0';

    // 处理相对路径，拼接完整路径
    char full_path[2048] = {0};
    // 处理"/"开头的linux绝对路径，以及win下类似于"C:\"、"AZ:\"之类的路径
    if (target_path[0] == '/' || (strlen(target_path) >= 2 && target_path[1] == ':') || (strlen(target_path) >= 3 && target_path[2] == ':')) {
        strncpy(full_path, target_path, sizeof(full_path) - 1);
    } else {
        // 相对路径：拼接到当前工作目录
        if (strlen(ctx->cwd) + 1 + strlen(target_path) >= sizeof(full_path)) {
            LOG_INFO("Path too long in CWD from %s:%d.\n", ctx->client_ip, ctx->client_port);
            if (snprintf(response, resp_size, "550 Path too long.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for path error.\n");
                return -1;
            }
            return 0;
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", ctx->cwd, target_path);
    }

    // 路径规范化与安全校验
    char norm_path[2048] = {0};
#ifdef _WIN32
    if (_fullpath(norm_path, full_path, sizeof(norm_path)) == NULL) {
        LOG_INFO("Invalid path in CWD: %s from %s:%d.\n", full_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Invalid path.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for path error.\n");
            return -1;
        }
        return 0;
    }

    // Windows 下路径分隔符规范化
    for (char* p = ctx->base_path; *p; ++p) {
        if (*p == '/') *p = '\\';
    }

    // 安全检查：目标路径必须在 base_path 内
    if (strncmp(norm_path, ctx->base_path, strlen(ctx->base_path)) != 0) {
        LOG_INFO("Access denied in CWD: %s from %s:%d.\n", target_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Access denied: Path outside allowed root.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for security error.\n");
            return -1;
        }
        return 0;
    }

    // 检查目录是否存在
    DWORD dwAttrib = GetFileAttributesA(norm_path);
    if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        LOG_INFO("Not a directory in CWD: %s from %s:%d.\n", norm_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path) >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.\n");
            return -1;
        }
        return 0;
    }
#else
    if (realpath(full_path, norm_path) == NULL) {
        LOG_INFO("Invalid path in CWD: %s from %s:%d.\n", full_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path + strlen(ctx->base_path)) >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.\n");
            return -1;
        }
        return 0;
    }

    // Linux 下路径分隔符规范化
    for (char* p = ctx->base_path; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

    // 安全检查：目标路径必须在 base_path 内
    if (strncmp(norm_path, ctx->base_path, strlen(ctx->base_path)) != 0) {
        LOG_INFO("Access denied in CWD: %s from %s:%d.\n", target_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Access denied: Path outside allowed root.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for security error.\n");
            return -1;
        }
        return 0;
    }

    // 检查目录是否存在
    struct stat st;
    if (stat(norm_path, &st) < 0 || !S_ISDIR(st.st_mode)) {
        LOG_INFO("Not a directory in CWD: %s from %s:%d.\n", norm_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path); >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 550 error.\n");
            return -1;
        }
        return 0;
    }
#endif

    // 更新当前工作目录
    strncpy(ctx->cwd, norm_path, sizeof(ctx->cwd) - 1);
    ctx->cwd[sizeof(ctx->cwd) - 1] = '\0';

    LOG_INFO("CWD successful for %s:%d, new cwd: %s\n", ctx->client_ip, ctx->client_port, ctx->cwd);

    // 发送 250 响应
    if (snprintf(response, resp_size, "250 Directory successfully changed.\r\n") >= (int)resp_size) {
        LOG_ERROR("Response buffer too small for 250 message.\n");
        return -1;
    }
    return 0;
}
