// Liaotian Server - 多客户端 TCP 聊天服务端
// 用法: server.exe [端口]   默认端口 12345
// 协议: 每条消息 = 4字节大端长度(uint32) + UTF-8 消息体
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

static const uint16_t DEFAULT_PORT = 12345;

struct Client {
    SOCKET sock;
    std::string name;
};

static std::vector<Client*> g_clients;
static std::mutex g_mtx;
static std::atomic<bool> g_running{true};

static bool recv_all(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

static bool send_all(SOCKET s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static void broadcast(const std::string& msg, SOCKET except) {
    uint32_t len = htonl((uint32_t)msg.size());
    std::vector<Client*> snapshot;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        snapshot = g_clients;
    }
    for (auto c : snapshot) {
        if (c->sock == except) continue;
        if (!send_all(c->sock, (char*)&len, 4) ||
            !send_all(c->sock, msg.data(), (int)msg.size())) {
            // 对方已断开，主循环会清理
        }
    }
}

static void client_thread(Client* client) {
    char lenbuf[4];
    while (g_running) {
        if (!recv_all(client->sock, lenbuf, 4)) break;
        uint32_t mlen = ntohl(*(uint32_t*)lenbuf);
        if (mlen == 0 || mlen > 65536) break;
        std::string msg(mlen, '\0');
        if (!recv_all(client->sock, &msg[0], (int)mlen)) break;
        // 客户端发来的是纯文本，直接广播
        broadcast(msg, client->sock);
    }

    // 移除并通知
    std::string leave = "【系统】" + client->name + " 离开了聊天";
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
            if (*it == client) { g_clients.erase(it); break; }
        }
    }
    broadcast(leave, client->sock);
    std::cout << "[-] " << client->name << " disconnected\n";
    closesocket(client->sock);
    delete client;
}

int main(int argc, char** argv) {
    uint16_t port = DEFAULT_PORT;
    if (argc >= 2) port = (uint16_t)atoi(argv[1]);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed (port " << port << ")\n";
        return 1;
    }
    listen(ls, 16);

    std::cout << "Liaotian server running on port " << port << "\n";
    std::cout << "Waiting for clients... (press Ctrl+C to quit)\n";

    while (g_running) {
        sockaddr_in ca{};
        int clen = sizeof(ca);
        SOCKET cs = accept(ls, (sockaddr*)&ca, &clen);
        if (cs == INVALID_SOCKET) break;

        // 先收昵称
        char lenbuf[4];
        if (!recv_all(cs, lenbuf, 4)) { closesocket(cs); continue; }
        uint32_t nlen = ntohl(*(uint32_t*)lenbuf);
        if (nlen == 0 || nlen > 256) { closesocket(cs); continue; }
        std::string name(nlen, '\0');
        recv_all(cs, &name[0], (int)nlen);

        Client* c = new Client{cs, name};
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_clients.push_back(c);
        }
        std::cout << "[+] " << name << " joined (" << inet_ntoa(ca.sin_addr) << ")\n";
        broadcast("【系统】" + name + " 加入了聊天", cs);
        std::thread(client_thread, c).detach();
    }

    closesocket(ls);
    WSACleanup();
    return 0;
}
