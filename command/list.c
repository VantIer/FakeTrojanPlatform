/**
 * @file list.c
 * @brief FTP LIST 命令实现
 */
#include "../utils.h"
#include "pasv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <direct.h>
    #include <sys/stat.h>
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <pwd.h>
    #include <grp.h>
    #define CLOSE_SOCKET(s) close(s)
#endif

static void format_mode(mode_t mode, char* buf)
{
    buf[0] = S_ISDIR(mode) ? 'd' : '-';
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
}

static void format_time(time_t ts, char* buf, size_t size)
{
    struct tm* tm_info = localtime(&ts);
    if (!tm_info) {
        snprintf(buf, size, "%12s", "???");
        return;
    }
    time_t now = time(NULL);
    struct tm* now_tm = localtime(&now);
    if (now_tm && tm_info->tm_year == now_tm->tm_year) {
        snprintf(buf, size, "%.3s %2d %02d:%02d",
            "JanFebMarAprMayJunJulAugSepOctNovDec" + tm_info->tm_mon * 3,
            tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min);
    } else {
        snprintf(buf, size, "%.3s %2d %5d",
            "JanFebMarAprMayJunJulAugSepOctNovDec" + tm_info->tm_mon * 3,
            tm_info->tm_mday, 1900 + tm_info->tm_year);
    }
}

static void get_username(unsigned long uid, char* buf, size_t size)
{
#ifdef _WIN32
    snprintf(buf, size, "%lu", uid);
#else
    struct passwd* pw = getpwuid((uid_t)uid);
    snprintf(buf, size, "%s", pw ? pw->pw_name : "root");
#endif
}

static void get_groupname(unsigned long gid, char* buf, size_t size)
{
#ifdef _WIN32
    snprintf(buf, size, "%lu", gid);
#else
    struct group* gr = getgrgid((gid_t)gid);
    snprintf(buf, size, "%s", gr ? gr->gr_name : "root");
#endif
}

int handle_ftp_list_command(const char* argument, char* response, size_t resp_size, client_context_t* ctx)
{
    if (!response || !resp_size || !ctx) {
        return -1;
    }

    if (ctx->mode == 0) {
        if (ctx->data_ip[0] == '\0' || ctx->port == 0) {
            snprintf(response, resp_size, "425 Use PORT or PASV first.\r\n");
            return 0;
        }
    } else if (ctx->mode != 1 || ctx->pasv_listen_sock == -1) {
        snprintf(response, resp_size, "425 Use PORT or PASV first.\r\n");
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
                LOG_INFO("Path too long in LIST from %s:%d.", ctx->client_ip, ctx->client_port);
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
    // 处理"/"开头的linux绝对路径，以及win下类似于"C:\"、"AZ:\"之类的路径
    if (target_path[0] == '/' || (strlen(target_path) >= 2 && target_path[1] == ':') || (strlen(target_path) >= 3 && target_path[2] == ':')) {
        strncpy(full_path, target_path, sizeof(full_path) - 1);
    } else {
        if (strlen(ctx->cwd) + 1 + strlen(target_path) >= sizeof(full_path)) {
            LOG_INFO("Path too long in LIST from %s:%d.", ctx->client_ip, ctx->client_port);
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
        LOG_INFO("Invalid path in LIST: %s from %s:%d.", full_path, ctx->client_ip, ctx->client_port);
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
        LOG_INFO("Access denied in LIST: %s from %s:%d.", target_path, ctx->client_ip, ctx->client_port);
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
        LOG_INFO("Access denied in LIST: %s from %s:%d.", target_path, ctx->client_ip, ctx->client_port);
        if (snprintf(response, resp_size, "550 Access denied: Path outside allowed root.\r\n") >= (int)resp_size) {
            LOG_ERROR("Response buffer too small for security error.");
            return -1;
        }
        return 0;
    }
#endif

    int data_sock = -1;
    if (ctx->mode == 0) {
        data_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (data_sock < 0) {
            snprintf(response, resp_size, "425 Failed to create data connection.\r\n");
            return 0;
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ctx->port);
        inet_pton(AF_INET, ctx->data_ip, &addr.sin_addr);
        if (connect(data_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            CLOSE_SOCKET(data_sock);
            snprintf(response, resp_size, "425 Cannot build data connection.\r\n");
            return 0;
        }
    } else {
        if (accept_ftp_data_connection(ctx, &data_sock, 60) != 0) {
            snprintf(response, resp_size, "425 Cannot build data connection.\r\n");
            return 0;
        }
    }

    snprintf(response, resp_size, "150 Opening ASCII mode data connection for file list.\r\n");
    send(ctx->sock, response, strlen(response), 0);

    char line[1024];
#ifdef _WIN32
    LOG_DEBUG("LIST: Checking path: %s", target_path);
    DWORD dwAttrib = GetFileAttributesA(target_path);
    LOG_DEBUG("LIST: GetFileAttributesA returned: 0x%08X", dwAttrib);
    int is_dir = (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
    LOG_DEBUG("LIST: is_dir = %d", is_dir);
    if (is_dir) {
        char search_path[MAX_PATH];
        snprintf(search_path, sizeof(search_path), "%s\\*", target_path);
        LOG_DEBUG("LIST: Searching with pattern: %s", search_path);
        WIN32_FIND_DATAA find_data;
        HANDLE hFind = FindFirstFileA(search_path, &find_data);
        LOG_DEBUG("LIST: FindFirstFileA returned: %p", hFind);
        if (hFind != INVALID_HANDLE_VALUE) {
            int entry_count = 0;
            do {
                entry_count++;
                const char* name = find_data.cFileName;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                int is_entry_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                unsigned long long size = ((unsigned long long)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
                time_t mod_time = 0;
                unsigned long long wt = ((unsigned long long)find_data.ftLastWriteTime.dwHighDateTime << 32) | find_data.ftLastWriteTime.dwLowDateTime;
                mod_time = (time_t)((wt / 10000000ULL) - 11644473600ULL);
                char time_str[32];
                format_time(mod_time, time_str, sizeof(time_str));
                LOG_DEBUG("LIST: Found entry #%d: name='%s', is_dir=%d, size=%llu",
                    entry_count, name, is_entry_dir, size);
                snprintf(line, sizeof(line), "%crwxr-xr-x   1 owner     group     %12llu %s %s\r\n",
                    is_entry_dir ? 'd' : '-', size, time_str, name);
                int sent = send(data_sock, line, strlen(line), 0);
                LOG_DEBUG("LIST: send() returned: %d, expected: %d", sent, (int)strlen(line));
            } while (FindNextFileA(hFind, &find_data));
            LOG_DEBUG("LIST: Finished listing %d entries", entry_count);
            FindClose(hFind);
        } else {
            LOG_DEBUG("LIST: FindFirstFileA failed, error=%lu", GetLastError());
        }
    } else {
        WIN32_FILE_ATTRIBUTE_DATA attr_data;
        if (GetFileAttributesExA(target_path, GetFileExInfoStandard, &attr_data)) {
            unsigned long long size = ((unsigned long long)attr_data.nFileSizeHigh << 32) | attr_data.nFileSizeLow;
            time_t mod_time = 0;
            unsigned long long wt = ((unsigned long long)attr_data.ftLastWriteTime.dwHighDateTime << 32) | attr_data.ftLastWriteTime.dwLowDateTime;
            mod_time = (time_t)((wt / 10000000ULL) - 11644473600ULL);
            char time_str[32];
            format_time(mod_time, time_str, sizeof(time_str));
            const char* name = strrchr(target_path, '\\');
            name = name ? name + 1 : target_path;
            LOG_DEBUG("LIST: Single file: name='%s', size=%llu", name, size);
            snprintf(line, sizeof(line), "-rw-r--r--   1 owner     group     %12llu %s %s\r\n",
                size, time_str, name);
            send(data_sock, line, strlen(line), 0);
        } else {
            LOG_DEBUG("LIST: GetFileAttributesExA failed, error=%lu", GetLastError());
        }
    }
#else
    LOG_DEBUG("LIST: Linux path = %s", target_path);
    struct stat st;
    if (stat(target_path, &st) < 0) {
        LOG_DEBUG("LIST: stat() failed, errno=%d", errno);
        snprintf(response, resp_size, "550 %s: No such file or directory.\r\n", target_path + strlen(ctx->base_path));
        CLOSE_SOCKET(data_sock);
        return 0;
    }
    LOG_DEBUG("LIST: stat() OK, st_mode=0x%08X, st_size=%ld", st.st_mode, (long)st.st_size);

    if (S_ISDIR(st.st_mode)) {
        LOG_DEBUG("LIST: Path is a directory");
        DIR* dir = opendir(target_path);
        if (dir) {
            struct dirent* entry;
            int entry_count = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                entry_count++;
                char full_entry_path[4096];
                snprintf(full_entry_path, sizeof(full_entry_path), "%s/%s", target_path, entry->d_name);
                struct stat entry_st;
                if (stat(full_entry_path, &entry_st) < 0) {
                    LOG_DEBUG("LIST: stat() failed for entry '%s'", entry->d_name);
                    continue;
                }
                char mode_str[11];
                format_mode(entry_st.st_mode, mode_str);
                char owner_str[32], group_str[32];
                get_username(entry_st.st_uid, owner_str, sizeof(owner_str));
                get_groupname(entry_st.st_gid, group_str, sizeof(group_str));
                char time_str[32];
                format_time(entry_st.st_mtime, time_str, sizeof(time_str));
                LOG_DEBUG("LIST: Entry #%d: name='%s', mode='%s', size=%ld",
                    entry_count, entry->d_name, mode_str, (long)entry_st.st_size);
                snprintf(line, sizeof(line), "%s   1 %-8s %-8s %10ld %s %s%s\r\n",
                    mode_str, owner_str, group_str, (long)entry_st.st_size,
                    time_str, entry->d_name, S_ISDIR(entry_st.st_mode) ? "/" : "");
                send(data_sock, line, strlen(line), 0);
            }
            LOG_DEBUG("LIST: Finished listing %d entries", entry_count);
            closedir(dir);
        } else {
            LOG_DEBUG("LIST: opendir() failed, errno=%d", errno);
        }
    } else {
        char mode_str[11];
        format_mode(st.st_mode, mode_str);
        char owner_str[32], group_str[32];
        get_username(st.st_uid, owner_str, sizeof(owner_str));
        get_groupname(st.st_gid, group_str, sizeof(group_str));
        char time_str[32];
        format_time(st.st_mtime, time_str, sizeof(time_str));
        const char* name = strrchr(target_path, '/');
        name = name ? name + 1 : target_path;
        LOG_DEBUG("LIST: Single file: name='%s', mode='%s', size=%ld",
            name, mode_str, (long)st.st_size);
        snprintf(line, sizeof(line), "%s   1 %-8s %-8s %10ld %s %s\r\n",
            mode_str, owner_str, group_str, (long)st.st_size, time_str, name);
        send(data_sock, line, strlen(line), 0);
    }
#endif

    LOG_DEBUG("LIST: Completed successfully");

    CLOSE_SOCKET(data_sock);
    snprintf(response, resp_size, "226 Transfer complete.\r\n");
    return 0;
}
