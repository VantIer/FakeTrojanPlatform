/**
 * @file utils.h
 * @brief 通用配置与工具宏定义头文件
 *
 * 本文件定义了服务器运行所需的基本配置参数，包括：
 * - 监听 IP 地址（默认 "0.0.0.0"）
 * - 监听端口号（默认 2121，FTP 常用非特权端口）
 * - 日志输出等级（支持 DEBUG, INFO, WARN, ERROR 四级）
 *
 * 所有配置均可通过预处理器宏在编译时覆盖，
 * 例如：gcc -DSERVER_IP=\"127.0.0.1\" -DSERVER_PORT=21 ...
 *
 * 日志等级说明：
 *   LOG_LEVEL_DEBUG (0) : 输出所有日志（含调试信息）
 *   LOG_LEVEL_INFO  (1) : 输出 info 及以上级别
 *   LOG_LEVEL_WARN  (2) : 输出警告及错误
 *   LOG_LEVEL_ERROR (3) : 仅输出错误
 *
 * 使用示例：
 *   #include "utils.h"
 *   if (CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO) {
 *       printf("[INFO] Server started.\n");
 *   }
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

/* ==================== 配置宏定义 ==================== */

// 默认监听 IP 地址：若未在编译时定义 SERVER_IP，则使用 "0.0.0.0"
#ifndef SERVER_IP
    #define SERVER_IP "0.0.0.0"
#endif

// 默认监听端口：若未在编译时定义 SERVER_PORT，则使用 2121（避免特权端口）
#ifndef SERVER_PORT
    #define SERVER_PORT 21
#endif

// 服务器名字
#ifndef SERVER_NAME
    #define SERVER_NAME "FakeTrojanPlatform"
#endif

// 登录服务器后的欢迎信息(用于指纹混淆)
#ifndef WELCOME_MSG
    #define WELCOME_MSG "220 FakeTrojanPlatform Ready\r\n"
#endif

/* ==================== 日志等级定义 ==================== */

// 日志等级枚举（数值越小，日志越详细）
#define LOG_LEVEL_DEBUG  0  /**< 调试级别：最详细 */
#define LOG_LEVEL_INFO   1  /**< 信息级别 */
#define LOG_LEVEL_WARN   2  /**< 警告级别 */
#define LOG_LEVEL_ERROR  3  /**< 错误级别：最简略 */
#define LOG_LEVEL_NOTHING  4  /**< 不记录日志 */

// 当前生效的日志等级（可通过编译选项 -DLOG_LEVEL=... 覆盖）
#ifndef LOG_LEVEL
    #define LOG_LEVEL LOG_LEVEL_DEBUG  // 默认为 INFO 级别
#endif

// 当前日志等级宏，便于条件编译或运行时判断
#define CURRENT_LOG_LEVEL LOG_LEVEL

/* ==================== 辅助日志宏（可选使用）==================== */

// 安全的字符串化宏
#define STR(x) #x
#define XSTR(x) STR(x)

// 条件日志输出宏（仅当当前日志等级允许时才输出）
#if CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG
    #define LOG_DEBUG(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) do { } while(0)
#endif

#if CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO
    #define LOG_INFO(fmt, ...) \
        fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_INFO(fmt, ...) do { } while(0)
#endif

#if CURRENT_LOG_LEVEL <= LOG_LEVEL_WARN
    #define LOG_WARN(fmt, ...) \
        fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_WARN(fmt, ...) do { } while(0)
#endif

#if CURRENT_LOG_LEVEL <= LOG_LEVEL_ERROR
    #define LOG_ERROR(fmt, ...) \
        fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_ERROR(fmt, ...) do { } while(0)
#endif

/* ==================== 程序通用宏 ==================== */

// 跨平台套接字头文件处理
#ifdef _WIN32
#include <process.h> // 用于 _beginthreadex
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

/**
 * @brief 客户端连接上下文结构体
 * 用于在线程启动时传递 socket 和客户端地址信息
 */
typedef struct {
    int sock; /**< 客户端 socket 描述符 */
    char client_ip[INET_ADDRSTRLEN]; /**< 客户端 IPv4 地址字符串（如 "192.168.1.100"） */
    unsigned short client_port; /**< 客户端端口号 */
    char username[256]; /**< 登录的用户名 */
    char password[256]; /**< 根据登录用户名查找到的密码，用于登录 */
    unsigned short user_status; /**< 用户状态，0未登录，1未验证密码，2已验证密码 */
    unsigned short admin; /**< 是否是管理员 */
    unsigned short admin_active; /**< 是否激活管理员 */
    unsigned short mode; /**< 主被动模式标记，0为主动PORT，1为被动PASV */
    char data_ip[16]; /**< 数据传输IP */
    unsigned short p1; /**< 端口数据 */
    unsigned short p2; /**< 端口数据 */
    unsigned short port; /**< 端口号 */
    char base_path[2048]; /**< 基础路径 */
    char cwd[2048]; /**< 当前工作目录 */
    
    // 被动模式相关字段
    int pasv_listen_sock; /**< 被动模式监听socket，-1表示未启用 */
    unsigned short pasv_port; /**< 被动模式分配的端口号 */
} client_context_t;

#endif /* UTILS_H */