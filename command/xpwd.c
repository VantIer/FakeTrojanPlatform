/**
 * @file xpwd.c
 * @brief FTP XPWD 命令实现
 *
 * 本文件实现 FTP XPWD（物理工作目录）命令。
 * XPWD 返回当前工作目录的物理路径。
 */
#include "../utils.h"
#include <string.h>

int handle_ftp_xpwd_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验
    if (argument == NULL || response == NULL || resp_size == 0 || ctx == NULL) {
        return -1;
    }

    // 获取当前工作目录
    const char* cwd = ctx->cwd;

    // 如果当前工作目录为空，则使用根目录
    if (cwd == NULL || strlen(cwd) == 0) {
        cwd = "/";
    }

    // 检查缓冲区大小
    size_t response_len = 5 + 2 + strlen(cwd) + 3 + 1; // "257 \"" + cwd + "\"\r\n" + \0

    if (resp_size < response_len) {
        LOG_ERROR("Response buffer too small for XPWD message.\n");
        return -1;
    }

    // 构建响应消息
    int written = snprintf(response, resp_size, "257 \"%s\"\r\n", cwd);

    if (written < 0 || (size_t)written >= resp_size) {
        LOG_ERROR("Failed to write XPWD response.\n");
        return -1;
    }

    LOG_DEBUG("XPWD command processed, current directory: %s\n", cwd);

    return 0;
}