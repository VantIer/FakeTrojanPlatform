#include "../utils.h"
#include <stdio.h>    // 用于 snprintf
#include <string.h>   // 用于 strlen, strspn, strcasecmp (Linux) / _stricmp (Windows)
#include <ctype.h>    // 可选，但此处未直接使用

// 兼容 Windows 与 Linux 的不区分大小写字符串比较
#ifdef _WIN32
    #include <string.h>
    #define STRCASECMP _stricmp
#else
    #include <strings.h> // 部分系统需要，但 string.h 通常已包含 strcasecmp
    #define STRCASECMP strcasecmp
#endif

/**
 * @brief 处理 FTP 的 OPTS 命令，特别是 "UTF8 ON" 子命令。
 *
 * 本函数解析客户端发送的 OPTS 命令参数，若为 "UTF8 ON"（忽略大小写和前后空格），
 * 则返回标准成功响应 "200 UTF8 set to on."；否则返回不支持错误。
 *
 * @param[in]  argument   指向命令参数字符串的指针（如 "UTF8 ON"）。不可为 NULL。
 * @param[out] response   指向输出缓冲区的指针，用于存储服务器响应字符串。
 *                        调用者需确保该缓冲区足够大（建议至少 64 字节）。
 * @param[in]  resp_size  response 缓冲区的大小（以字节为单位），用于防止缓冲区溢出。
 *
 * @return int            返回 0 表示成功生成响应；返回 -1 表示参数无效或缓冲区太小。
 *
 * @note 本实现遵循 RFC 2640，对 UTF-8 支持返回 200 状态码。
 *       忽略参数中的前导/尾随空格，并进行不区分大小写的比较。
 */
int handle_ftp_opts_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx) {
    // 参数校验：确保输入参数有效
    if (argument == NULL || response == NULL || resp_size == 0) {
        return -1; // 无效参数
    }

    // 跳过前导空格
    const char* start = argument + strspn(argument, " \t\r\n");
    
    // 计算有效字符串长度（去除尾部空格）
    size_t len = strlen(start);
    if (len == 0) {
        // 若参数为空，则返回不支持
        if (snprintf(response, resp_size, "504 Option not supported.\r\n") >= (int)resp_size) {
            LOG_ERROR("No Argument Found.");
            return -1; // 缓冲区不足
        }
        return 0;
    }

    // 去除尾部空格：找到最后一个非空白字符的位置
    while (len > 0 && strchr(" \t\r\n", start[len - 1]) != NULL) {
        len--;
    }

    // 创建一个临时栈上缓冲区用于存放去空格后的字符串（带 null 终止）
    // 最大长度不会超过原始参数长度 + 1
    char trimmed_arg[256]; // FTP 参数通常很短，256 足够
    if (len >= sizeof(trimmed_arg)) {
        // 安全截断（理论上不应发生，但防御性编程）
        len = sizeof(trimmed_arg) - 1;
    }
    memcpy(trimmed_arg, start, len);
    trimmed_arg[len] = '\0';

    // 检查是否为 "UTF8 ON"（不区分大小写）
    if (STRCASECMP(trimmed_arg, "UTF8 ON") == 0) {
        // 成功启用 UTF-8，返回标准 200 响应
        if (snprintf(response, resp_size, "200 UTF8 set to on.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 200 message.");
            return -1; // 缓冲区太小，无法完整写入
        }
    } else if (STRCASECMP(trimmed_arg, "UTF8 OFF") == 0) {
        // 成功启用 UTF-8，返回标准 200 响应
        if (snprintf(response, resp_size, "200 Always in UTF8 mode.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 200 message.");
            return -1; // 缓冲区太小，无法完整写入
        }
    } else {
        // 其他 OPTS 参数暂不支持
        if (snprintf(response, resp_size, "504 Option not supported.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for 504 error.");
            return -1;
        }
    }

    return 0; // 成功
}