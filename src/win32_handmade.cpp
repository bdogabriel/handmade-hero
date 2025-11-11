#include <windows.h>
#include <wingdi.h>
#include <winuser.h>

LRESULT CALLBACK MainWindowCallback(HWND Wnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    LRESULT result = 0;

    switch (Msg)
    {
    case WM_SIZE: {
        OutputDebugStringA("WM_SIZE\n");
    }
    break;

    case WM_DESTROY: {
        OutputDebugStringA("WM_DESTROY\n");
    }
    break;

    case WM_CLOSE: {
        OutputDebugStringA("WM_CLOSE\n");
    }
    break;

    case WM_ACTIVATEAPP: {
        OutputDebugStringA("WM_ACTIVATEAPP\n");
    }
    break;

    case WM_PAINT: {
        PAINTSTRUCT Paint;
        HDC DeviceCtx = BeginPaint(Wnd, &Paint);
        int x = Paint.rcPaint.left;
        int y = Paint.rcPaint.top;
        int w = Paint.rcPaint.right - Paint.rcPaint.left;
        int h = Paint.rcPaint.bottom - Paint.rcPaint.top;
        static DWORD Color = WHITENESS;
        OutputDebugStringA("WM_PAINT\n");
        PatBlt(DeviceCtx, x, y, w, h, Color);
        if (Color == WHITENESS)
        {
            Color = BLACKNESS;
        }
        else
        {
            Color = WHITENESS;
        }
        EndPaint(Wnd, &Paint);
    }
    break;

    default: {
        // OutputDebugStringA("default\n");
        result = DefWindowProcA(Wnd, Msg, WParam, LParam);
    }
    break;
    }

    return result;
}

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int ShowCmd)
{
    WNDCLASS WndClass = {};
    WndClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    WndClass.lpfnWndProc = MainWindowCallback;
    WndClass.hInstance = Instance;
    // WindowClass.hIcon;
    WndClass.lpszClassName = "HandmadeHeroWindowClass";

    if (RegisterClassA(&WndClass))
    {
        HWND Wnd = CreateWindowExA(0, WndClass.lpszClassName, "Handmade Hero", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, Instance, 0);

        if (Wnd)
        {
            MSG Msg;
            BOOL MsgRes = GetMessageA(&Msg, 0, 0, 0);
            while (MsgRes > 0)
            {
                TranslateMessage(&Msg);
                DispatchMessageA(&Msg);
                MsgRes = GetMessageA(&Msg, 0, 0, 0);
            }
        }
        else
        {
            // TODO: log
        }
    }
    else
    {
        // TODO: log
    }

    return 0;
}
