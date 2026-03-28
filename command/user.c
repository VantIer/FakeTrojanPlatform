#include "../utils.h"
#include <stdio.h> // 标准输入输出，用于文件操作
#include <stdlib.h> // 标准库，如 malloc、free
#include <string.h> // 字符串操作

/**
 * @brief 从 config.ini 文件中读取合法的用户名和密码，并存入会话上下文
 *
 * 本函数解析当前目录下的 config.ini 文件，期望其格式为每行一个键值对，
 * 且不含多余空格，例如：
 *     [admin]
 *     password=123456
 *     admin=1
 *
 * 成功读取后，将管理员状态和密码分别保存到会话上下文 `ctx` 的对应字段中。
 *
 * @param[out] user_in  指向缓冲区的指针，用于返回读取到的用户名（可选，若非 NULL 则复制用户名至此）
 * @param[in,out] ctx   指向客户端会话上下文的指针，用于存储配置中的管理员状态和密码
 *
 * @return int 成功读取并解析配置文件时返回 0；若文件无法打开、格式错误或内存操作失败，则返回 -1
 */
static int read_config_ini(char* user_in, client_context_t* ctx)
{
    FILE* fp = NULL;
    char line[512];
    char temp[512];
    int found_user = 0;

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

        // 构造用户名标签 [username]
        snprintf(temp, sizeof(temp), "[%s]", user_in);

        // 解析 [username]
        if (strncmp(line, temp, strlen(temp)) == 0) {
            // 查找用户
            found_user = 1;
        } else if (strncmp(line, "[", 1) == 0) {
            // 没找到
            found_user = 0;
        } else if (found_user == 1 && strncmp(line, "password=", 9) == 0) {
            // 找到用户，获取密码
            const char* value = line + 9;
            strncpy(ctx->password, value, sizeof(ctx->password) - 1);
            ctx->password[sizeof(ctx->password) - 1] = '\0';
            ctx->user_status = 1;
        } else if (found_user == 1 && strncmp(line, "admin=1", 7) == 0) {
            // 获取管理员信息
            ctx->admin = 1;
        }
    }

    fclose(fp);

    return 0;
}

/**
 * @brief 处理 FTP 客户端发送的 USER 命令
 *
 * 本函数解析客户端在 USER 命令中提供的用户名，并将其暂存于会话上下文 `ctx` 中。
 * 同时，从配置文件 config.ini 中加载合法的用户名与对应密码，也存入 `ctx`，
 * 为后续的 PASS 命令认证做准备。
 *
 * 注意：此阶段不进行用户名合法性校验，仅完成参数记录与配置加载。
 *       实际的身份验证将在接收到 PASS 命令后执行。
 *
 * @param[in]  argument    指向 USER 命令参数的字符串（即客户端提交的用户名）
 * @param[out] response    用于填充响应消息的缓冲区（如 "331 User name okay, need password."）
 * @param[in]  resp_size   response 缓冲区的大小（字节数），用于防止溢出
 * @param[in,out] ctx      指向当前客户端会话上下文的指针，用于保存用户状态及配置信息
 *
 * @return int 成功时返回 0；若参数无效、配置读取失败或内存操作出错，则返回 -1
 */
int handle_ftp_user_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    // 参数校验：确保输入参数有效
    if (argument == NULL || response == NULL || resp_size == 0 || ctx == NULL) {
        return -1; // 无效参数
    }

    // 清空上下文中的输入用户名等信息
    memset(ctx->username, 0, sizeof(ctx->username));
    memset(ctx->password, 0, sizeof(ctx->password));
    ctx->user_status = 0;
    ctx->admin = 0;

    // 保存客户端提供的用户名（去除可能的回车换行）
    size_t arg_len = strlen(argument);
    if (arg_len >= sizeof(ctx->username)) {
        arg_len = sizeof(ctx->username) - 1;
    }
    memcpy(ctx->username, argument, arg_len);
    ctx->username[arg_len] = '\0';

    // 从 config.ini 读取合法的用户名和密码
    if (read_config_ini(ctx->username, ctx) != 0) {
        // 读取配置失败
        return -1;
    }

    LOG_INFO("User \"%s\" try to login. User %s, Admin %s.", ctx->username, ctx->user_status == 1 ? "Found" : "Not Found", ctx->admin == 1 ? "YES" : "NO");

    if (snprintf(response, resp_size, "331 User name okay, need password.\r\n") >= (int)resp_size) {
        LOG_ERROR("Response buffer too small for 331 message.");
        return -1; // 缓冲区不足
    }

    return 0; // 成功
}