#include <dsound.h>
#include <math.h>
#include <stdint.h>
#include <windows.h>
#include <xinput.h>

#define PI 3.14159265359
#define BYTES_PER_PIXEL 4

#define internal static
#define local_persist static
#define global static

#define XINPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef XINPUT_GET_STATE(x_input_get_state);
XINPUT_GET_STATE(XInputGetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define XINPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef XINPUT_SET_STATE(x_input_set_state);
XINPUT_SET_STATE(XInputSetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

struct win32_offscreen_buffer
{
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;

    int MemorySize;
    int Pitch;
};

struct win32_window_dimension
{
    int Width;
    int Height;
};

struct win32_sound_output
{
    int16_t ToneVolume = 4000;
    int ToneHz = 256;
    int SampleHz = 48000;
    int BytesPerSample = sizeof(int16_t) * 2;
    int BufferSize = SampleHz * BytesPerSample;
    int WavePeriod = SampleHz / ToneHz;
    uint32_t RunningSampleIndex = 0;
};

// TODO: global for now
global bool GlobalRunning = false;
global win32_offscreen_buffer GlobalBackBuffer;
global LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;

internal void Win32LoadXInput()
{
    HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");

    if (!XInputLibrary)
    {
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }

    // TODO: log loaded version

    if (XInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal void Win32InitDSound(HWND Window, DWORD SampleHz, DWORD BufferSize)
{
    HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");

    if (DSoundLibrary)
    {
        direct_sound_create *DirectSoundCreate =
            (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");
        LPDIRECTSOUND DirectSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
        {
            WAVEFORMATEX WaveFormat = {};
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 2;
            WaveFormat.nSamplesPerSec = SampleHz;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = WaveFormat.nChannels * WaveFormat.wBitsPerSample / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;

            if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
            {
                DSBUFFERDESC BufferDesc = {};
                BufferDesc.dwSize = sizeof(BufferDesc);
                BufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

                LPDIRECTSOUNDBUFFER PrimaryBuffer;
                if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDesc, &PrimaryBuffer, 0)))
                {
                    // primary buffer requires SetFormat
                    if (SUCCEEDED(PrimaryBuffer->SetFormat(&WaveFormat)))
                    {
                        OutputDebugStringA("Primary buffer wave format set.\n");
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
            }
            else
            {
                // TODO: log
            }

            DSBUFFERDESC BufferDesc = {};
            BufferDesc.dwSize = sizeof(BufferDesc);
            BufferDesc.dwFlags = 0;
            BufferDesc.dwBufferBytes = BufferSize;
            BufferDesc.lpwfxFormat = &WaveFormat;

            if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDesc, &GlobalSecondaryBuffer, 0)))
            {
                OutputDebugStringA("Secondary buffer created.\n");
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
    }
}

internal void Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD ByteToLock, DWORD BytesToWrite)
{
    VOID *Region1;
    VOID *Region2;
    DWORD Region1Size;
    DWORD Region2Size;

    if (SUCCEEDED(
            GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite, &Region1, &Region1Size, &Region2, &Region2Size, 0)))
    {
        int16_t *SampleOut = (int16_t *)Region1;
        DWORD SampleCount = Region1Size / SoundOutput->BytesPerSample;
        for (DWORD SampleIndex = 0; SampleIndex < SampleCount; SampleIndex++)
        {
            float T = 2.0f * PI * (float)SoundOutput->RunningSampleIndex / (float)SoundOutput->WavePeriod;
            int16_t SampleValue = (int16_t)(sinf(T) * SoundOutput->ToneVolume);
            *SampleOut++ = SampleValue;
            *SampleOut++ = SampleValue;
            SoundOutput->RunningSampleIndex++;
        }

        SampleOut = (int16_t *)Region2;
        SampleCount = Region2Size / SoundOutput->BytesPerSample;
        for (DWORD SampleIndex = 0; SampleIndex < SampleCount; SampleIndex++)
        {
            float T = 2.0f * PI * (float)SoundOutput->RunningSampleIndex / (float)SoundOutput->WavePeriod;
            int16_t SampleValue = (int16_t)(sinf(T) * SoundOutput->ToneVolume);
            *SampleOut++ = SampleValue;
            *SampleOut++ = SampleValue;
            SoundOutput->RunningSampleIndex++;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void Win32RenderBitmap(win32_offscreen_buffer *Buffer, int XOffset, int YOffset)
{
    uint8_t *Row = (uint8_t *)Buffer->Memory;

    for (int Y = 0; Y < Buffer->Height; Y++)
    {
        uint32_t *Pixel = (uint32_t *)Row;

        for (int X = 0; X < Buffer->Width; X++)
        {
            uint8_t Blue = X + XOffset;
            uint8_t Green = Y + YOffset;
            *Pixel++ = (Green << 8) | Blue;
        }

        Row += Buffer->Pitch;
    }
}

internal void Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    if (Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->Pitch = Width * BYTES_PER_PIXEL;
    Buffer->MemorySize = (Width * Height) * BYTES_PER_PIXEL;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height; // top-down
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    Buffer->Memory = VirtualAlloc(0, Buffer->MemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    // TODO: clear to black
}

internal void Win32DisplayBuffer(win32_offscreen_buffer *Buffer, HDC DeviceCtx, int WindowWidth, int WindowHeight)
{
    // TODO: proper strech and aspect ratio
    StretchDIBits(DeviceCtx, 0, 0, WindowWidth, WindowHeight, 0, 0, Buffer->Width, Buffer->Height, Buffer->Memory,
                  &Buffer->Info, DIB_RGB_COLORS, SRCCOPY);
}

internal win32_window_dimension Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;
    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;
    return Result;
}

LRESULT CALLBACK Win32MainWindowCallback(HWND Window, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    LRESULT result = 0;

    switch (Msg)
    {
    case WM_SIZE: {
        OutputDebugStringA("WM_SIZE\n");
    }
    break;

    case WM_CLOSE: {
        // TODO: handle with message to the user
        GlobalRunning = false;
    }
    break;

    case WM_DESTROY: {
        // TODO: handle as error -> recreate window
        GlobalRunning = false;
    }
    break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        uint32_t VKCode = WParam;
        bool WasDown = LParam & (1 << 30);
        bool IsDown = !(LParam & (1 << 31));

        if (IsDown == WasDown)
        {
            break;
        }

        switch (VKCode)
        {
        case 'W': {
            OutputDebugStringA("W\n");
        }
        break;
        case 'A': {
            OutputDebugStringA("A\n");
        }
        break;
        case 'S': {
            OutputDebugStringA("S\n");
        }
        break;
        case 'D': {
            OutputDebugStringA("D\n");
        }
        break;
        case 'Q': {
            OutputDebugStringA("D\n");
        }
        break;
        case 'E': {
            OutputDebugStringA("E\n");
        }
        break;
        case VK_UP: {
            OutputDebugStringA("Up\n");
        }
        break;
        case VK_LEFT: {
            OutputDebugStringA("Left\n");
        }
        break;
        case VK_RIGHT: {
            OutputDebugStringA("Right\n");
        }
        break;
        case VK_DOWN: {
            OutputDebugStringA("Down\n");
        }
        break;
        case VK_ESCAPE: {
            OutputDebugStringA("Escape\n");
        }
        break;
        case VK_SPACE: {
            OutputDebugStringA("Space\n");
        }
        break;
        case VK_F4: {
            if (LParam & (1 << 29)) // alt
            {
                GlobalRunning = false;
            }
        }
        break;
        default: {
        }
        break;
        }
    }
    break;

    case WM_ACTIVATEAPP: {
        OutputDebugStringA("WM_ACTIVATEAPP\n");
    }
    break;

    case WM_PAINT: {
        PAINTSTRUCT Paint;
        HDC DeviceCtx = BeginPaint(Window, &Paint);
        win32_window_dimension Dimension = Win32GetWindowDimension(Window);
        Win32DisplayBuffer(&GlobalBackBuffer, DeviceCtx, Dimension.Width, Dimension.Height);
        EndPaint(Window, &Paint);
    }
    break;

    default: {
        result = DefWindowProcA(Window, Msg, WParam, LParam);
    }
    break;
    }

    return result;
}

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int ShowCmd)
{
    Win32LoadXInput();

    WNDCLASSA WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

    WindowClass.style = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    // WindowClass.hIcon;
    WindowClass.lpszClassName = "HandmadeHeroWindowClass";

    if (RegisterClassA(&WindowClass))
    {
        HWND Window = CreateWindowExA(0, WindowClass.lpszClassName, "Handmade Hero", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, Instance, 0);

        if (Window)
        {
            GlobalRunning = true;

            int XOffset = 0;
            int YOffset = 0;

            win32_sound_output SoundOutput;
            Win32InitDSound(Window, SoundOutput.SampleHz, SoundOutput.BufferSize);
            Win32FillSoundBuffer(&SoundOutput, 0, SoundOutput.BufferSize);
            GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            while (GlobalRunning)
            {
                MSG Msg;
                while (PeekMessage(&Msg, 0, 0, 0, PM_REMOVE))
                {
                    if (Msg.message == WM_QUIT)
                    {
                        GlobalRunning = false;
                    }

                    TranslateMessage(&Msg);
                    DispatchMessageA(&Msg);
                }

                for (DWORD ControllerIndex = 0; ControllerIndex < XUSER_MAX_COUNT; ControllerIndex++)
                {
                    XINPUT_STATE ControllerState;
                    if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                    {
                        // TODO: check if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

                        bool Up = Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP;
                        bool Down = Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN;
                        bool Left = Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT;
                        bool Right = Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
                        bool A = Pad->wButtons & XINPUT_GAMEPAD_A;
                        bool B = Pad->wButtons & XINPUT_GAMEPAD_B;
                        bool X = Pad->wButtons & XINPUT_GAMEPAD_X;
                        bool Y = Pad->wButtons & XINPUT_GAMEPAD_Y;
                        bool Start = Pad->wButtons & XINPUT_GAMEPAD_START;
                        bool Back = Pad->wButtons & XINPUT_GAMEPAD_BACK;
                        bool LeftSholder = Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER;
                        bool RightSholder = Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER;

                        int16_t LStickX = Pad->sThumbLX;
                        int16_t LStickY = Pad->sThumbLY;

                        XOffset -= LStickX >> 12;
                        YOffset += LStickY >> 12;
                    }
                    else
                    {
                        // TODO: handle controller not available
                    }
                }

                Win32RenderBitmap(&GlobalBackBuffer, XOffset, YOffset);

                DWORD PlayCursor;
                DWORD WriteCursor;

                if (SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
                {
                    DWORD BytesToWrite = 0;
                    DWORD ByteToLock =
                        (SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) % SoundOutput.BufferSize;

                    if (ByteToLock > PlayCursor)
                    {
                        BytesToWrite = PlayCursor + SoundOutput.BufferSize - ByteToLock;
                    }
                    else if (ByteToLock < PlayCursor)
                    {
                        BytesToWrite = PlayCursor - ByteToLock;
                    }

                    Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite);
                }

                HDC DeviceCtx = GetDC(Window);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBuffer(&GlobalBackBuffer, DeviceCtx, Dimension.Width, Dimension.Height);
                ReleaseDC(Window, DeviceCtx);
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
