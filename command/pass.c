#include "../utils.h"
#include <stdio.h> // 标准输入输出，用于文件操作
#include <stdlib.h> // 标准库，如 malloc、free
#include <string.h> // 字符串操作

/**
 * @brief 处理 FTP 客户端发送的 PASS 命令（密码验证）
 *
 * 本函数接收客户端在 PASS 命令中提交的密码字符串，并与会话上下文 `ctx` 中
 * 已通过配置文件加载的合法密码进行安全比对。若匹配成功，则标记用户已认证；
 * 否则返回认证失败。
 *
 * 注意：
 *   - 本函数假设在调用前已通过 USER 命令调用 `read_config_ini()` 成功加载了合法密码到 `ctx->password`。
 *   - 密码比较采用恒定时间比较（避免时序攻击），即使长度不同也遍历全部字符。
 *   - 不直接暴露密码内容到日志，防止信息泄露。
 *
 * @param[in]  argument    指向 PASS 命令参数的字符串（即客户端提交的密码）
 * @param[out] response    用于填充响应消息的缓冲区（如 "230 User logged in." 或 "530 Login incorrect."）
 * @param[in]  resp_size   response 缓冲区的大小（字节数），用于防止溢出
 * @param[in,out] ctx      指向当前客户端会话上下文的指针，其中应包含合法密码 `password`
 *                         和用户认证状态字段（如 `user_status`）
 *
 * @return int 
 *         - 0 表示成功处理（无论认证成功或失败，只要流程正常）
 *         - -1 表示参数无效或内部错误（如 response 缓冲区太小）
 *
 * @note 调用者需确保 `ctx->password` 已由之前的 USER 命令正确加载。
 */
int handle_ftp_pass_authentication(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验：确保所有输入输出指针有效且缓冲区非零
    if (argument == NULL || response == NULL || resp_size == 0 || ctx == NULL) {
        return -1; // 无效参数
    }

    // 获取客户端提供的密码（去除末尾可能的 \r\n）
    // 注意：FTP 协议中命令行以 \r\n 结尾，但 argument 通常已剥离，此处做安全截断
    const char* client_pass = argument;
    size_t client_len = strlen(client_pass);
    // 若存在回车或换行符，提前截断（虽然通常不会出现）
    for (size_t i = 0; i < client_len; ++i) {
        if (client_pass[i] == '\r' || client_pass[i] == '\n') {
            client_len = i;
            break;
        }
    }

    // 获取合法密码（来自 config.ini，已在 USER 阶段加载到 ctx->password
    const char* valid_pass = ctx->password;
    size_t valid_len = strlen(valid_pass);

    // 恒定时间密码比较（constant-time comparison）以防御时序攻击
    // 即使长度不同，也强制比较 max(len1, len2) 字节
    size_t max_len = (client_len > valid_len) ? client_len : valid_len;
    int match = 0;

    // 如果长度不同，match 初始设为 1（表示不匹配），但仍继续比较所有字节
    if (client_len != valid_len) {
        match = 1;
    }
    // 用户状态不正确
    if (ctx->user_status != 1) {
        match = 1;
    }

    // 逐字节异或比较，避免短路
    for (size_t i = 0; i < max_len; ++i) {
        char c1 = (i < client_len) ? client_pass[i] : 0;
        char c2 = (i < valid_len) ? valid_pass[i] : 0;
        match |= (c1 ^ c2); // 若任一字节不同，match 非零
    }

    // 根据比较结果设置响应和认证状态
    if (match == 0) {
        // 认证成功
        ctx->user_status = 2; // 设置用户状态为登录
        LOG_INFO("User \"%s\" login Success. Admin %s.", ctx->username, ctx->admin == 1 ? "YES" : "NO");
        if (snprintf(response, resp_size, "230 User logged in.\r\n") >= (int)resp_size) {
            // 缓冲区不足，无法写入完整响应
            LOG_ERROR("Response buffer too small for 230 message.");
            return -1;
        }
    } else {
        // 认证失败
        ctx->user_status = 0;
        LOG_WARN("User \"%s\" login FAILED. Admin %s.", ctx->username, ctx->admin == 1 ? "YES" : "NO");
        if (snprintf(response, resp_size, "530 Login incorrect.\r\n") >= (int)resp_size) {
            // 缓冲区不足，无法写入完整响应
            LOG_ERROR("Response buffer too small for 530 error.");
            return -1;
        }
    }

    return 0; // 成功处理 PASS 命令
}