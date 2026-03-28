# FakeTrojanPlatform

AI辅助编码的，以FTP通信协议为模板，将远程控制通信指令与数据融入其中，支持标准FTP客户端的远程控制工具

顺便也作为学习尝试AI辅助编码的测试工程



---

## 主要思路

将常见的控制指令及数据结构，按照文件目录结构进行存放，然后使用标准FTP协议进行传输，实现支持标准FTP客户端的远程控制工具效果

例如：

Process（目录） -> Process1.exe（文件）、Process2.exe（文件）、Process3.exe（文件）...

下载该文件则代表读取该进程信息。

删除文件代表杀死该进程



---

# Build

纯C语言，无三方组件以来，Win下开发，使用codeblocks编译通过

编码时候预留了Linux的兼容代码，但是还没写Linux下的Makefile



注意windows下使用codeblocks编译时，需要配置项目引入libws2_32.a和libwsock32.a

这俩文件在codeblocks安装路径MinGW/x86_64-w64-mingw32/lib下



---

# Todo

FTP标准指令完善    Doing

FTP标准指令与C2功能转接实现    Waiting

C2指令    Waiting

Win测试    Doing

Linux测试    Waiting


