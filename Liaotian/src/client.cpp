// Liaotian Client - Win32 GUI 聊天客户端
// 用法: client.exe
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define ID_CONNECT   1001
#define ID_NICK      1002
#define ID_HOST      1003
#define ID_PORT      1004
#define ID_CHATLOG   1005
#define ID_INPUT     1006
#define ID_SEND      1007

#define WM_RECVMSG (WM_USER + 100)

static HINSTANCE g_hInst;
static HWND g_hMain, g_hNick, g_hHost, g_hPort, g_hConnect;
static HWND g_hLog, g_hInput, g_hSend;
static SOCKET g_sock = INVALID_SOCKET;
static std::atomic<bool> g_recvRunning{false};
static std::thread g_recvThread;

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring get_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n, 0);
    GetWindowTextW(h, &s[0], n + 1);
    return s;
}

static bool send_packet(const std::string& msg) {
    uint32_t len = htonl((uint32_t)msg.size());
    int sent = 0;
    while (sent < 4) {
        int n = send(g_sock, (char*)&len + sent, 4 - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    sent = 0;
    while (sent < (int)msg.size()) {
        int n = send(g_sock, msg.data() + sent, (int)msg.size() - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static void append_log(const std::string& utf8) {
    std::wstring line = utf8_to_wide(utf8) + L"\r\n";
    int idx = SendMessageW(g_hLog, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    SendMessageW(g_hLog, LB_SETTOPINDEX, idx, 0);
}

static void recv_loop() {
    char lenbuf[4];
    while (g_recvRunning) {
        int got = 0;
        while (got < 4) {
            int n = recv(g_sock, lenbuf + got, 4 - got, 0);
            if (n <= 0) goto disconnected;
            got += n;
        }
        uint32_t mlen = ntohl(*(uint32_t*)lenbuf);
        if (mlen == 0 || mlen > 65536) goto disconnected;
        std::string msg(mlen, '\0');
        got = 0;
        while (got < (int)mlen) {
            int n = recv(g_sock, &msg[got], (int)mlen - got, 0);
            if (n <= 0) goto disconnected;
            got += n;
        }
        PostMessageW(g_hMain, WM_RECVMSG, 0, (LPARAM)new std::string(msg));
    }
disconnected:
    g_recvRunning = false;
    PostMessageW(g_hMain, WM_RECVMSG, 0, (LPARAM)new std::string("【系统】与服务器的连接已断开"));
}

static void disconnect() {
    g_recvRunning = false;
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    if (g_recvThread.joinable()) g_recvThread.join();
    SetWindowTextW(g_hConnect, L"连接");
}

static bool do_connect() {
    std::wstring nick = get_text(g_hNick);
    std::wstring host = get_text(g_hHost);
    std::wstring port_s = get_text(g_hPort);
    if (nick.empty() || host.empty() || port_s.empty()) return false;
    uint16_t port = (uint16_t)_wtoi(port_s.c_str());

    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonW(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // 尝试当作主机名
        struct addrinfoW hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (GetAddrInfoW(host.c_str(), nullptr, &hints, &res) != 0) {
            closesocket(g_sock); g_sock = INVALID_SOCKET;
            return false;
        }
        sockaddr_in* p = (sockaddr_in*)res->ai_addr;
        addr.sin_addr = p->sin_addr;
        FreeAddrInfoW(res);
    }

    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_sock); g_sock = INVALID_SOCKET;
        return false;
    }

    // 发送昵称
    std::string nick_utf8 = wide_to_utf8(nick);
    if (!send_packet(nick_utf8)) {
        closesocket(g_sock); g_sock = INVALID_SOCKET;
        return false;
    }

    SetWindowTextW(g_hConnect, L"断开");
    append_log("【系统】已连接到 " + wide_to_utf8(host) + ":" + wide_to_utf8(port_s));
    g_recvRunning = true;
    g_recvThread = std::thread(recv_loop);
    return true;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hNick   = CreateWindowExW(0, L"EDIT", L"我", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
                                    10, 10, 80, 24, h, (HMENU)ID_NICK, g_hInst, nullptr);
        g_hHost   = CreateWindowExW(0, L"EDIT", L"127.0.0.1", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
                                    100, 10, 140, 24, h, (HMENU)ID_HOST, g_hInst, nullptr);
        g_hPort   = CreateWindowExW(0, L"EDIT", L"12345", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
                                    245, 10, 60, 24, h, (HMENU)ID_PORT, g_hInst, nullptr);
        g_hConnect= CreateWindowExW(0, L"BUTTON", L"连接", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                    310, 10, 80, 24, h, (HMENU)ID_CONNECT, g_hInst, nullptr);

        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                                  WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
                                  10, 45, 480, 300, h, (HMENU)ID_CHATLOG, g_hInst, nullptr);

        g_hInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                                    10, 355, 400, 24, h, (HMENU)ID_INPUT, g_hInst, nullptr);
        g_hSend  = CreateWindowExW(0, L"BUTTON", L"发送", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                    415, 355, 75, 24, h, (HMENU)ID_SEND, g_hInst, nullptr);

        HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
        SendMessageW(g_hNick, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hHost, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hPort, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hConnect, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hInput, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_hSend, WM_SETFONT, (WPARAM)f, TRUE);
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        MoveWindow(g_hLog, 10, 45, W - 20, H - 85, TRUE);
        MoveWindow(g_hInput, 10, H - 30, W - 95, 24, TRUE);
        MoveWindow(g_hSend, W - 80, H - 30, 70, 24, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(w);
        if (id == ID_CONNECT) {
            if (g_sock != INVALID_SOCKET) { disconnect(); }
            else {
                if (!do_connect()) {
                    MessageBoxW(h, L"连接失败，请检查服务器地址和端口", L"错误", MB_OK|MB_ICONERROR);
                }
            }
        } else if (id == ID_SEND) {
            if (g_sock == INVALID_SOCKET) {
                MessageBoxW(h, L"请先连接服务器", L"提示", MB_OK);
                return 0;
            }
            std::wstring text = get_text(g_hInput);
            if (text.empty()) return 0;
            std::wstring nick = get_text(g_hNick);
            std::string line = wide_to_utf8(nick) + ": " + wide_to_utf8(text);
            if (!send_packet(line)) {
                disconnect();
                append_log("【系统】发送失败，连接已断开");
            } else {
                append_log(line);
                SetWindowTextW(g_hInput, L"");
            }
        }
        return 0;
    }
    case WM_RECVMSG: {
        std::string* p = (std::string*)l;
        append_log(*p);
        delete p;
        return 0;
    }
    case WM_DESTROY:
        disconnect();
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd) {
    g_hInst = hInst;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LiaotianClient";
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, L"LiaotianClient", L"聊天客户端",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 520, 420,
                              nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hMain, nCmd);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
