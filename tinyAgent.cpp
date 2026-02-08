/* 
 * TinyAgent C++ Backend (v19.1 - Extended Click + Hotkey Fix)
 * Compiles with: 
 * g++ tinyAgent.cpp -o tinyagent.exe -lws2_32 -lgdi32 -lgdiplus -luser32 -lkernel32 -lole32 -luuid -lshell32 -mwindows -static
 */

#define NOMINMAX 
#define _WIN32_WINNT 0x0600 
#define WIN32_LEAN_AND_MEAN 

#include <winsock2.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <algorithm> 
#include <cmath> 

using namespace Gdiplus;

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"kernel32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")

#define PORT 8080
#define ID_HOTKEY_FULL 101 // Ctrl + F9
#define ID_HOTKEY_ACTIVE 102 // Ctrl + F10

// State
volatile int g_server_running = 1;

// Data Protection
CRITICAL_SECTION g_img_lock; 
char *g_last_screenshot_b64 = NULL;
size_t g_last_screenshot_len = 0;
int g_has_new_screenshot = 0;

DWORD g_main_thread_id = 0;
CLSID g_pngClsid;

// Context
int g_capture_mode = 0; 
HWND g_target_hwnd = NULL; 
RECT g_last_capture_rect = {0}; 

// --- HELPER: Kill previous instance ---
void KillOldInstance() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        const char* msg = "POST /shutdown HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(s, msg, strlen(msg), 0);
        closesocket(s);
        Sleep(1000); 
    } else {
        closesocket(s);
    }
}

// --- HELPER: Network Send ---
int send_full(SOCKET s, const char *buf, size_t len) {
    size_t total = 0;
    size_t bytesleft = len;
    while(total < len) {
        int chunk = (bytesleft > 16384) ? 16384 : (int)bytesleft;
        int n = send(s, buf+total, chunk, 0);
        if (n == -1) { return -1; }
        total += n;
        bytesleft -= n;
    }
    return 0;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT  num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if(pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for(UINT j = 0; j < num; ++j) {
        if(wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    static const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = (char*)malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }
    const int mod_table[] = {0, 2, 1};
    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

void VisualFlash(int x, int y, int w, int h) {
    HDC hDC = GetDC(NULL);
    PatBlt(hDC, x, y, w, h, DSTINVERT);
    Sleep(50);
    PatBlt(hDC, x, y, w, h, DSTINVERT);
    ReleaseDC(NULL, hDC);
}

void CaptureScreen(int type) {
    printf("[Backend] Capture type: %d\n", type);
    
    int x, y, w, h;
    RECT rc = {0};
    g_capture_mode = type;
    // Always capture the foreground window HWND, even in full screen mode,
    // so we can restore focus to it later.
    g_target_hwnd = GetForegroundWindow();
    if(!g_target_hwnd) g_target_hwnd = GetDesktopWindow();

    if (type == 1) { // Full
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else { // Active Window (Smart Tracking)
        GetWindowRect(g_target_hwnd, &rc);
        x = rc.left; y = rc.top; w = rc.right - rc.left; h = rc.bottom - rc.top;
    }

    if (w <= 0 || h <= 0) return;

    MessageBeep(MB_OK);
    VisualFlash(x, y, w, h);

    g_last_capture_rect.left = x; g_last_capture_rect.top = y; 
    g_last_capture_rect.right = x+w; g_last_capture_rect.bottom = y+h;

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    
    BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

    IStream *pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    
    Bitmap *pBitmap = Bitmap::FromHBITMAP(hBitmap, NULL);
    pBitmap->Save(pStream, &g_pngClsid, NULL);
    delete pBitmap;

    HGLOBAL hGlobal = NULL;
    GetHGlobalFromStream(pStream, &hGlobal);
    void *pData = GlobalLock(hGlobal);
    size_t size = GlobalSize(hGlobal);
    
    size_t b64_len;
    char *b64 = base64_encode((unsigned char*)pData, size, &b64_len);
    
    EnterCriticalSection(&g_img_lock);
    if(g_last_screenshot_b64) free(g_last_screenshot_b64);
    g_last_screenshot_b64 = (char*)malloc(b64_len + 64);
    strcpy(g_last_screenshot_b64, "data:image/png;base64,");
    strcat(g_last_screenshot_b64, b64);
    g_last_screenshot_len = strlen(g_last_screenshot_b64);
    g_has_new_screenshot = 1;
    LeaveCriticalSection(&g_img_lock);
    
    printf("[Backend] Snapshot ready. Size: %lu bytes.\n", (unsigned long)g_last_screenshot_len);

    free(b64);
    GlobalUnlock(hGlobal);
    pStream->Release();
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

// --- LOGIC (SendInput) ---

WORD GetVkFromString(const char* key) {
    if (strcmp(key, "Control") == 0) return VK_CONTROL;
    if (strcmp(key, "Shift") == 0) return VK_SHIFT;
    if (strcmp(key, "Alt") == 0) return VK_MENU;
    if (strcmp(key, "Enter") == 0) return VK_RETURN;
    if (strcmp(key, "Esc") == 0) return VK_ESCAPE;
    if (strcmp(key, "Space") == 0) return VK_SPACE;
    if (strcmp(key, "Backspace") == 0) return VK_BACK;
    if (strcmp(key, "Tab") == 0) return VK_TAB;
    // Single characters
    if (strlen(key) == 1) return VkKeyScan(key[0]) & 0xFF;
    return 0;
}

void PerformKey(const char* keyStr, bool down) {
    WORD vk = GetVkFromString(keyStr);
    if (!vk) return;
    
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (!down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void SendKeyInput(WORD vk, BOOL up) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (up) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void PerformType(const char *text) {
    if (!text) return;
    printf("[Backend] Typing (Unicode): %s\n", text);

    // Convert UTF-8 to WideChar (UTF-16)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen <= 0) return;
    
    wchar_t *wstr = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, wlen);

    for(int i=0; i < wlen - 1; i++) { // -1 to skip null terminator
        wchar_t ch = wstr[i];
        
        INPUT inputs[2] = {};
        
        // Key Down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = 0;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        
        // Key Up
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 0;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        
        SendInput(2, inputs, sizeof(INPUT));
        Sleep(20);
    }
    free(wstr);
}

void PerformScroll(int delta) {
    printf("[Backend] Scrolling: %d\n", delta);
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = (DWORD)delta;
    SendInput(1, &input, sizeof(INPUT));
}

void PerformClick(const char* btn, int x, int y, int dbl) {
    SetCursorPos(x, y);
    
    DWORD downParams = MOUSEEVENTF_LEFTDOWN;
    DWORD upParams = MOUSEEVENTF_LEFTUP;
    
    if (strcmp(btn, "right") == 0) {
        downParams = MOUSEEVENTF_RIGHTDOWN;
        upParams = MOUSEEVENTF_RIGHTUP;
    } else if (strcmp(btn, "middle") == 0) {
        downParams = MOUSEEVENTF_MIDDLEDOWN;
        upParams = MOUSEEVENTF_MIDDLEUP;
    }

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = downParams;
    SendInput(1, &input, sizeof(INPUT));
    
    input.mi.dwFlags = upParams;
    SendInput(1, &input, sizeof(INPUT));

    if (dbl) { 
        Sleep(100); 
        input.mi.dwFlags = downParams;
        SendInput(1, &input, sizeof(INPUT));
        input.mi.dwFlags = upParams;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void PerformDrag(const char* btn, int x1, int y1, int x2, int y2) {
    SetCursorPos(x1, y1); 
    
    DWORD downParams = MOUSEEVENTF_LEFTDOWN;
    DWORD upParams = MOUSEEVENTF_LEFTUP;
    if (strcmp(btn, "right") == 0) { downParams = MOUSEEVENTF_RIGHTDOWN; upParams = MOUSEEVENTF_RIGHTUP; }
    else if (strcmp(btn, "middle") == 0) { downParams = MOUSEEVENTF_MIDDLEDOWN; upParams = MOUSEEVENTF_MIDDLEUP; }

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = downParams;
    SendInput(1, &input, sizeof(INPUT));
    
    Sleep(100);
    for(int i=0; i<=10; i++) { 
        SetCursorPos(x1 + (x2-x1)*i/10, y1 + (y2-y1)*i/10); 
        Sleep(10); 
    }
    
    input.mi.dwFlags = upParams;
    SendInput(1, &input, sizeof(INPUT));
}

void GetTargetCoords(int normX, int normY, int *outX, int *outY) {
    int w, h, offsetX, offsetY;
    // Если режим окна и оно валидно - используем его
    if (g_capture_mode == 2 && g_target_hwnd && IsWindow(g_target_hwnd)) {
        RECT rc; GetWindowRect(g_target_hwnd, &rc);
        offsetX = rc.left; offsetY = rc.top;
        w = rc.right - rc.left; h = rc.bottom - rc.top;
    } else {
        // Иначе координаты последнего снимка (полный экран)
        offsetX = g_last_capture_rect.left; offsetY = g_last_capture_rect.top;
        w = g_last_capture_rect.right - g_last_capture_rect.left;
        h = g_last_capture_rect.bottom - g_last_capture_rect.top;
    }
    if (w <= 0) w = GetSystemMetrics(SM_CXSCREEN);
    if (h <= 0) h = GetSystemMetrics(SM_CYSCREEN);
    *outX = offsetX + (normX * w / 1000);
    *outY = offsetY + (normY * h / 1000);
}

int ExecuteScript(const char *code, float speed) {
    printf("[Backend] Executing script...\n");
    
    // Focus Window
    // Focus Window
    // Always try to restore the captured window, regardless of mode
    if (g_target_hwnd && IsWindow(g_target_hwnd)) {
        printf("[Backend] Focusing target window...\n");
        
        // METHOD 0: Disable Foreground Lock Timeout (Session wide setting)
        // This prevents Windows from flashing the button in taskbar instead of focusing.
        DWORD timeout = 0;
        SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)(size_t)timeout, SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);

        // Unlock SetForegroundWindow if locked
        LockSetForegroundWindow(2); // LSFW_UNLOCK

        // Try multiple methods to force focus
        if(IsIconic(g_target_hwnd)) ShowWindow(g_target_hwnd, SW_RESTORE);
        
        // Method 0: Allow
        AllowSetForegroundWindow(ASFW_ANY);

        // Method 1: SwitchToThisWindow (often more aggressive)
        SwitchToThisWindow(g_target_hwnd, TRUE);

        // Method 1.5: Simulate Input to trick Windows
        // Press and release Shift
        keybd_event(VK_SHIFT, 0, 0, 0);
        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);

        // Method 2: AttachThreadInput hack (Enabled for robustness)
        DWORD myId = GetCurrentThreadId();
        DWORD targetId = GetWindowThreadProcessId(g_target_hwnd, NULL);
        if (myId != targetId) {
            AttachThreadInput(myId, targetId, TRUE);
            BringWindowToTop(g_target_hwnd);
            SetForegroundWindow(g_target_hwnd);
            AttachThreadInput(myId, targetId, FALSE);
        }
        
        Sleep(300); 
    }

    POINT last_pos; GetCursorPos(&last_pos);
    char *code_copy = strdup(code);
    char *line = strtok(code_copy, "\n");
    
    while(line) {
        // Kill Switch - Reverted to 100px per user request
        POINT curr_pos; GetCursorPos(&curr_pos);
        double dist = sqrt(pow(curr_pos.x - last_pos.x, 2) + pow(curr_pos.y - last_pos.y, 2));
        if (dist > 100.0) { 
            printf("[Backend] ABORT: Mouse moved dist=%.2f (>100.0). Last(%ld,%ld) Curr(%ld,%ld)\n", 
                dist, last_pos.x, last_pos.y, curr_pos.x, curr_pos.y);
            free(code_copy); return -1; 
        }

        int p1, p2, p3, p4; char s_arg[256]; char s_btn[32];
        int tx, ty, tx2, ty2;

        if (sscanf(line, "drag ( \"%[^\"]\" , %d , %d , %d , %d )", s_btn, &p1, &p2, &p3, &p4) == 5) {
            GetTargetCoords(p1, p2, &tx, &ty);
            GetTargetCoords(p3, p4, &tx2, &ty2);
            PerformDrag(s_btn, tx, ty, tx2, ty2);
            // Wait a bit for OS to update cursor pos, then trust actual pos
            Sleep(50);
            GetCursorPos(&last_pos); 
        }
        else if (sscanf(line, "click ( \"%[^\"]\" , %d , %d )", s_btn, &p1, &p2) == 3) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick(s_btn, tx, ty, 0);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "click ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick("left", tx, ty, 0);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "dblclick ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick("left", tx, ty, 1);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "move ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            SetCursorPos(tx, ty);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "wait ( %d )", &p1) == 1) {
            int chunks = p1 / 100; if (chunks == 0) chunks = 1;
            for(int i=0; i<chunks; i++) {
                Sleep((DWORD)(100 / speed));
                GetCursorPos(&curr_pos);
                if (sqrt(pow(curr_pos.x - last_pos.x, 2) + pow(curr_pos.y - last_pos.y, 2)) > 100.0) {
                     printf("[Backend] ABORT (Wait): Mouse moved.\n");
                     free(code_copy); return -1;
                }
            }
        }
        else if (sscanf(line, "type ( \"%[^\"]\" )", s_arg) == 1) {
            PerformType(s_arg);
        }
        else if (sscanf(line, "scroll ( %d )", &p1) == 1) {
            PerformScroll(p1);
        }
        else if (sscanf(line, "keyDown ( \"%[^\"]\" )", s_arg) == 1) {
            PerformKey(s_arg, true);
        }
        else if (sscanf(line, "keyUp ( \"%[^\"]\" )", s_arg) == 1) {
            PerformKey(s_arg, false);
        }
        
        Sleep((DWORD)(50 / speed));
        line = strtok(NULL, "\n");
    }
    free(code_copy);
    printf("[Backend] Script finished.\n");
    return 0;
}

DWORD WINAPI ServerThread(LPVOID lpParam) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) return 0;
    listen(server_fd, 3);
    
    HWND hExist = FindWindowA(NULL, "TinyAgent UI v19");
    if (hExist) {
        SetForegroundWindow(hExist);
    } else {
        ShellExecuteA(NULL, "open", "http://localhost:8080", NULL, NULL, SW_SHOWNORMAL);
    }

    while(g_server_running) {
        SOCKET new_socket = accept(server_fd, NULL, NULL);
        if(new_socket == INVALID_SOCKET) continue;

        char buffer[8192] = {0}; 
        recv(new_socket, buffer, sizeof(buffer)-1, 0);

        if (strstr(buffer, "GET / ")) {
            FILE *f = fopen("index.html", "rb");
            if(f) {
                fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
                char *html = (char*)malloc(fsize + 1); fread(html, 1, fsize, f); fclose(f);
                char header[512]; 
                sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)fsize);
                send_full(new_socket, header, strlen(header));
                send_full(new_socket, html, fsize);
                free(html);
            }
        } else if (strstr(buffer, "GET /heartbeat")) {
            const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
            send_full(new_socket, resp, strlen(resp));
        } else if (strstr(buffer, "POST /shutdown")) {
            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nBye";
            send_full(new_socket, resp, strlen(resp));
            g_server_running = 0; PostThreadMessage(g_main_thread_id, WM_QUIT, 0, 0);
        } else if (strstr(buffer, "GET /events")) {
            for(int i=0; i<30; i++) { if(g_has_new_screenshot || !g_server_running) break; Sleep(100); }
            EnterCriticalSection(&g_img_lock);
            if(g_has_new_screenshot && g_last_screenshot_b64) {
                g_has_new_screenshot = 0;
                const char *prefix = "{\"type\":\"screenshot\",\"image\":\"";
                const char *suffix = "\"}";
                size_t json_len = strlen(prefix) + g_last_screenshot_len + strlen(suffix);
                char header[512];
                sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)json_len);
                send_full(new_socket, header, strlen(header));
                send_full(new_socket, prefix, strlen(prefix));
                send_full(new_socket, g_last_screenshot_b64, g_last_screenshot_len);
                send_full(new_socket, suffix, strlen(suffix));
            } else {
                const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
                send_full(new_socket, resp, strlen(resp));
            }
            LeaveCriticalSection(&g_img_lock);
        } else if (strstr(buffer, "POST /run")) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4;
                float speed = 1.0;
                char *speed_start = strstr(body, "\"speed\":"); if(speed_start) sscanf(speed_start + 8, "%f", &speed);
                char *code_start = strstr(body, "\"code\":");
                if(code_start) {
                    code_start += 7; char *qs = strchr(code_start, '"');
                    if (qs) {
                        char script[1024*10] = {0}; int si=0; qs++; 
                        while(*qs) {
                            if(*qs == '"' && *(qs-1) != '\\') break; 
                            if(*qs == '\\' && *(qs+1) == 'n') { script[si++]='\n'; qs+=2; continue; }
                            if(*qs == '\\' && *(qs+1) == '"') { script[si++]='"'; qs+=2; continue; }
                            script[si++] = *qs++;
                        }
                        
                        int res = ExecuteScript(script, speed);

                        if (res == -1) {
                            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nABORTED";
                            send_full(new_socket, resp, strlen(resp));
                        } else {
                            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK";
                            send_full(new_socket, resp, strlen(resp));
                        }
                    }
                }
            }
        }
        closesocket(new_socket);
    }
    return 0;
}

int main() {
    g_main_thread_id = GetCurrentThreadId();
    InitializeCriticalSection(&g_img_lock);
    KillOldInstance(); 
    SetProcessDPIAware(); 
    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    GetEncoderClsid(L"image/png", &g_pngClsid);

    RegisterHotKey(NULL, ID_HOTKEY_FULL, 0, VK_F9);
    RegisterHotKey(NULL, ID_HOTKEY_ACTIVE, 0, VK_F10);

    CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0) != 0) {
        if (msg.message == WM_HOTKEY) {
            int type = 1; // Full
            if (msg.wParam == ID_HOTKEY_ACTIVE) type = 2; // Active
            CaptureScreen(type);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    DeleteCriticalSection(&g_img_lock);
    return 0;
}