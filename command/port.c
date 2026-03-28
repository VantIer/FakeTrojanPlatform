#include "../utils.h"
#include <stdio.h>   // 标准输入输出
#include <stdlib.h>  // atoi, strtol 等
#include <string.h>  // 字符串操作
#include <stdint.h>  // uint16_t

/**
 * @brief 处理 FTP 客户端发送的 PORT 命令（主动模式数据连接地址通知）
 *
 * 本函数解析客户端发送的 PORT 命令参数，格式为 "h1,h2,h3,h4,p1,p2"，
 * 其中 h1～h4 为 IPv4 地址的四个十进制字节，p1 和 p2 为端口号的高/低字节。
 * 成功解析后，将客户端数据连接 IP、p1、p2 及完整端口号存入 ctx。
 *
 * 注意：
 *   - 严格校验参数数量（必须为6个非负整数，范围 0～255）
 *   - 不支持 IPv6 或其他扩展格式
 *   - 若解析失败，返回错误响应；成功则返回 200 响应
 *
 * @param[in]  argument    指向 PORT 命令参数的字符串（如 "192,168,1,100,10,200"）
 * @param[out] response    用于填充响应消息的缓冲区（如 "200 PORT command successful." 或 "501 Invalid PORT format."）
 * @param[in]  resp_size   response 缓冲区大小，防止溢出
 * @param[in,out] ctx      指向当前客户端会话上下文，用于存储解析结果
 *
 * @return int
 *         - 0 表示成功处理（无论参数是否合法，只要流程正常）
 *         - -1 表示内部错误（如 response 缓冲区太小或 ctx 为空）
 *
 * @note 调用者需确保 ctx 已初始化，且 argument 不为 NULL。
 */
int handle_ftp_port_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验：确保所有指针有效且缓冲区非零
    if (argument == NULL || response == NULL || resp_size == 0 || ctx == NULL) {
        return -1;
    }

    // 创建可修改的副本，因为 strtok 会修改字符串
    char* arg_copy = malloc(strlen(argument) + 1);
    if (arg_copy == NULL) {
        LOG_ERROR("Failed to allocate memory for PORT argument copy.");
        return -1;
    }
    strcpy(arg_copy, argument);

    // 使用 strtok 分割逗号分隔的参数
    char* token;
    int values[6];
    int count = 0;

    token = strtok(arg_copy, ",");
    while (token != NULL && count < 6) {
        // 去除前后空格（虽然 FTP 通常无空格，但做安全处理）
        char* start = token;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0') {
            break; // 空 token
        }
        char* end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
        }

        // 尝试转换为整数
        char* num_end;
        long val = strtol(start, &num_end, 10);

        // 检查是否完全转换且在 0～255 范围内
        if (*num_end != '\0' || val < 0 || val > 255) {
            free(arg_copy);
            if (snprintf(response, resp_size, "501 Invalid PORT format.\r\n") >= (int)resp_size) {
                LOG_ERROR("Response buffer too small for error message.");
                return -1;
            }
            return 0;
        }

        values[count++] = (int)val;
        token = strtok(NULL, ",");
    }

    free(arg_copy);

    // 必须恰好有6个参数
    if (count != 6) {
        if (snprintf(response, resp_size, "501 PORT requires exactly 6 comma-separated numbers.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for error message.");
            return -1;
        }
        return 0;
    }

    // 构造 IP 字符串 "h1.h2.h3.h4"
    int h1 = values[0], h2 = values[1], h3 = values[2], h4 = values[3];
    int p1 = values[4], p2 = values[5];

    // 格式化 IP 地址字符串
    if (snprintf(ctx->data_ip, sizeof(ctx->data_ip), "%d.%d.%d.%d", h1, h2, h3, h4) >= (int)sizeof(ctx->data_ip)) {
        // 理论上不会发生，因为 15 字节足够（"255.255.255.255" = 15 字符）
        LOG_WARN("IP string truncated in ctx->data_ip.");
    }

    // 存储 p1, p2 和完整端口
    ctx->p1 = (unsigned char)p1;
    ctx->p2 = (unsigned char)p2;
    ctx->port = (uint16_t)((p1 << 8) | p2); // p1 * 256 + p2

    // 标记 PORT 已设置
    ctx->mode = 0;

    // 返回成功响应
    if (snprintf(response, resp_size, "200 PORT command successful.\r\n") >= (int)resp_size) {
        LOG_ERROR("Response buffer too small for success message.");
        return -1;
    }

    return 0;
}