*如果你喜欢它，就给它点上star吧~~*
# 聊天软件 (Liaotian)

用 C++ / Win32 API 写的多客户端文字聊天工具，已静态编译为 exe，**对方不需要安装任何编译器或运行库**，双击即可使用。

## 文件说明

```
D:\Liaotian\
├── server.exe     # 服务端（先启动这个）
├── client.exe      # 客户端（每个人都运行这个）
└── src\
    ├── server.cpp  # 服务端源码
    └── client.cpp  # 客户端源码
```

## 使用方法

### 1. 启动服务端（选一台有公网/局域网 IP 的电脑）

双击 `server.exe`，默认监听 `12345` 端口。
如果要换端口：在命令行运行 `server.exe 8080`。

### 2. 每个用户启动客户端

双击 `client.exe`，在顶部填写：
- **昵称**：你在群里的显示名
- **服务器**：服务端那台电脑的 IP（本机测试填 `127.0.0.1`）
- **端口**：和服务端一致，默认 `12345`

点「连接」即可开始聊天。所有在线用户都会收到你发的消息。

### 3. 局域网/外网联机

- **同一局域网**：服务端电脑打开 cmd 运行 `ipconfig`，找 IPv4 地址（如 `192.168.x.x`），客户端填这个地址。
- **外网联机**：需要在路由器上把 `12345` 端口映射到服务端电脑，客户端填公网 IP。
- **Windows 防火墙**：第一次运行 server.exe 时，允许它通过防火墙（否则别人连不上）。

## 功能

- 多人同时在线，文字群聊
- 中文消息正常显示（UTF-8 传输）
- 加入/离开自动广播系统通知
- 客户端图形界面：聊天记录、输入框、发送按钮
- 服务端控制台显示在线用户变动

## 重新编译（可选）

需要 MinGW g++：
```
g++ -O2 -static -static-libgcc -static-libstdc++ -o server.exe src\server.cpp -lws2_32
g++ -O2 -municode -static -static-libgcc -static-libstdc++ -mwindows -o client.exe src\client.cpp -lws2_32 -luser32 -lgdi32
```
