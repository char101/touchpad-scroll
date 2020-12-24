#include "resource.h"
#include <Windows.h>
#include <commctrl.h>
#include <iostream>
#include <strsafe.h>

#define APP_NAME L"touchpad-scroll"

#define TOUCHPAD L"\\\\?\\HID#VID_0C45&PID_8101&MI_01#7&290539a9&0&0000#{378de44c-56ef-11d1-bc8c-00a0c91405dd}"

#define WM_USER_SHELLICON WM_USER + 1
#define WM_TASKBAR_CREATE RegisterWindowMessage(L"TaskbarCreated")

#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)
#define WFILE WIDE1(__FILE__)
#define WFUNCTION WIDE1(__FUNCTION__)
#define DEBUG(fmt, ...) debugLog(WFILE, __LINE__, WFUNCTION, fmt, __VA_ARGS__)
#define WARNING(fmt, ...) showWarning(WFILE, __LINE__, WFUNCTION, fmt, __VA_ARGS__)

HANDLE gSingleInstanceMutex;
HWND gHwnd;
HINSTANCE gInstance;
NOTIFYICONDATA gTrayIconData;
HANDLE gDevice;

void debugLog(const wchar_t *file, int line, const wchar_t *func, const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    wchar_t inner[4096];
    vswprintf_s(inner, sizeof(inner), fmt, args);
    va_end(args);

    wchar_t outer[4096 + 1024];
    swprintf_s(outer, sizeof(outer), L"[%s] %s:%d (%s) %s", APP_NAME, file, line, func, inner);

    OutputDebugStringW(outer);
}

void showWarning(const wchar_t *file, int line, const wchar_t *func, const wchar_t *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    wchar_t inner[4096];
    vswprintf_s(inner, sizeof(inner), fmt, args);
    va_end(args);

    wchar_t outer[4096 + 1024];
    swprintf_s(outer, sizeof(outer), L"[%s] %s:%d (%s) %s", APP_NAME, file, line, func, inner);

    MessageBoxW(0, outer, APP_NAME, MB_ICONWARNING | MB_OK);
}

HANDLE findRawInputDevice()
{
    UINT nDevices;
    if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0) {
        DEBUG(L"GetRawInputDeviceList failed");
        return nullptr;
    }
    if (nDevices == 0) {
        return nullptr;
    }
    PRAWINPUTDEVICELIST pRawInputDeviceList;
    if ((pRawInputDeviceList = (PRAWINPUTDEVICELIST)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices)) == NULL) {
        DEBUG(L"pRawInputDeviceList malloc failed");
        return nullptr;
    }
    if (GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST)) == -1) {
        DEBUG(L"GetRawInputDeviceList failed");
        return nullptr;
    }
    HANDLE touchpad = nullptr;
    for (UINT i = 0; i < nDevices; ++i) {
        const RAWINPUTDEVICELIST &device = pRawInputDeviceList[i];
        if (device.dwType == RIM_TYPEMOUSE) {
            UINT nameSize;
            GetRawInputDeviceInfo(device.hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
            if (nameSize) {
                TCHAR *name = new TCHAR[nameSize + 1];
                if (GetRawInputDeviceInfo(device.hDevice, RIDI_DEVICENAME, name, &nameSize) != (UINT)-1) {
                    if (wcscmp(name, TOUCHPAD) == 0) {
                        DEBUG(L"Raw device: %s hDevice: %p", name, device.hDevice);
                        touchpad = device.hDevice;
                    }
                }
                delete[] name;
            }
        }
    }
    free(pRawInputDeviceList);
    return touchpad;
}

bool setupRawInput()
{
    RAWINPUTDEVICE rid;

    // mouse
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK; // if RIDEV_NOLEGACY is specified then the tray icon context menu does not work
    rid.hwndTarget = gHwnd;        // required if RIDEV_INPUTSINK is used

    if (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE) {
        WARNING(L"RegisterRawInputDevices failed: %d", GetLastError());
        return false;
    }

    return true;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // cannot use in switch because WM_TASKBAR_CREATE is not a constant
    if (message == WM_TASKBAR_CREATE) {
        // taskbar has been recreated (Explorer crashed?)
        if (!Shell_NotifyIcon(NIM_ADD, &gTrayIconData)) {
            WARNING(L"Shell_NotifyIcon failed");
            DestroyWindow(gHwnd);
            return -1;
        }
    }

    switch (message) {
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &gTrayIconData);
        PostQuitMessage(0);
        return 0;
    case WM_USER_SHELLICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            HMENU menu = LoadMenu(gInstance, MAKEINTRESOURCE(IDR_POPUP_MENU));
            if (!menu) {
                WARNING(L"LoadMenu failed");
                return -1;
            }

            HMENU submenu = GetSubMenu(menu, 0);
            if (!submenu) {
                DestroyMenu(menu);
                WARNING(L"GetSubMenu failed");
                return -1;
            }

            // Display menu
            POINT pos;
            GetCursorPos(&pos);
            SetForegroundWindow(gHwnd);
            TrackPopupMenu(submenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, pos.x, pos.y, 0, gHwnd, NULL);
            SendMessage(gHwnd, WM_NULL, 0, 0);

            DestroyMenu(menu);
            return 0;
        }
        }
        break;
    case WM_CLOSE:
        DestroyWindow(gHwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_POPUP_EXIT:
            DestroyWindow(gHwnd);
            return 0;
        }
        break;
    case WM_INPUT:
        if (wParam == RIM_INPUTSINK) {
            // Get data size
            UINT dwSize;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

            LPBYTE lpb = new BYTE[dwSize];
            if (lpb != NULL) {
                bool handled = false;
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != -1) {
                    RAWINPUT *raw = (RAWINPUT *)lpb;
                    if (raw->header.hDevice == gDevice && raw->data.mouse.usFlags == 0x0 && raw->data.mouse.usButtonFlags == 0x0 &&
                        raw->data.mouse.lLastY != 0) {
                        // TCHAR szTempOutput[2048];
                        // HRESULT hResult = StringCchPrintf(szTempOutput, STRSAFE_MAX_CCH, TEXT("Mouse: lLastY=%04x hDevice=%p"),
                        //                                   raw->data.mouse.lLastY, raw->header.hDevice);
                        // DEBUG(szTempOutput);

                        INPUT input;
                        input.type = INPUT_MOUSE;
                        input.mi.dx = 0;
                        input.mi.dy = -raw->data.mouse.lLastY;
                        input.mi.time = 0;
                        input.mi.dwExtraInfo = 0;
                        input.mi.dwFlags = MOUSEEVENTF_WHEEL | MOUSEEVENTF_MOVE;
                        input.mi.mouseData = raw->data.mouse.lLastY * WHEEL_DELTA / 6;
                        SendInput(1, &input, sizeof(INPUT));

                        handled = true;
                    }
                }
                delete[] lpb;
                if (handled) {
                    return 0;
                }
            }
        }
        break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int cleanup()
{
    DEBUG(L"cleanup");
    CloseHandle(gSingleInstanceMutex);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    // Check for running instance
    gSingleInstanceMutex = CreateMutex(NULL, FALSE, L"{1abf48f2-725c-4377-8e5a-176265a6b0d4}");
    if (gSingleInstanceMutex == NULL) {
        WARNING(L"CreateMutex: %d", GetLastError());
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        WARNING(L"Already running");
        return 0;
    }

    // Save instance handles
    gInstance = hInstance;

    // Initialize common controls
    INITCOMMONCONTROLSEX iccex;
    iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    iccex.dwICC = ICC_UPDOWN_CLASS | ICC_LISTVIEW_CLASSES;
    if (!InitCommonControlsEx(&iccex)) {
        WARNING(0, L"InitCommonControlsEx failed");
        return -1;
    }

    // Create window
    WNDCLASSEX wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, (LPCTSTR)MAKEINTRESOURCE(IDI_TRAYICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = APP_NAME;
    wc.hIconSm = LoadIcon(hInstance, (LPCTSTR)MAKEINTRESOURCE(IDI_TRAYICON));
    if (!RegisterClassEx(&wc)) {
        WARNING(L"RegisterClassEx failed");
        return -1;
    }
    gHwnd = CreateWindowEx(WS_EX_CLIENTEDGE, APP_NAME, APP_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           NULL, NULL, hInstance, NULL);
    if (gHwnd == NULL) {
        WARNING(L"CreateWindowEx failed");
        return -1;
    }

    // Setup icon data
    gTrayIconData.cbSize = sizeof(NOTIFYICONDATA);
    gTrayIconData.hWnd = (HWND)gHwnd;
    gTrayIconData.uID = IDI_TRAYICON;
    gTrayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    wcscpy(gTrayIconData.szTip, APP_NAME);
    gTrayIconData.hIcon = LoadIcon(hInstance, (LPCTSTR)MAKEINTRESOURCE(IDI_TRAYICON));
    gTrayIconData.uCallbackMessage = WM_USER_SHELLICON;

    // Display tray icon
    if (!Shell_NotifyIcon(NIM_ADD, &gTrayIconData)) {
        WARNING(L"Failed creating tray icon");
        return -1;
    }

    gDevice = findRawInputDevice();
    if (!gDevice) {
        WARNING(L"Cannot find device");
        cleanup();
        return -1;
    }

    // Setup raw input
    if (!setupRawInput()) {
        cleanup();
        return -1;
    }

    // Start message loop
    BOOL ret;
    MSG msg;
    while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (ret == -1) {
            WARNING(L"GetMessage: ret == -1");
            cleanup();
            return -1;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return cleanup();
}
