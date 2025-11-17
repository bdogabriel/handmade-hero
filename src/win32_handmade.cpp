#include <dsound.h>
#include <math.h> // TODO: implement sine to remove math.h
#include <stdint.h>
#include <windows.h>
#include <xinput.h>

#define PI 3.14159265359
#define BYTES_PER_PIXEL 4

#define internal static
#define local_persist static
#define global static

#define XINPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef XINPUT_GET_STATE(XInputGetState_);
XINPUT_GET_STATE(xinput_get_state_stub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global XInputGetState_ *xinput_get_state = xinput_get_state_stub;
#define XInputGetState xinput_get_state

#define XINPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef XINPUT_SET_STATE(XInputSetState_);
XINPUT_SET_STATE(xinput_set_state_stub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global XInputSetState_ *xinput_set_state = xinput_set_state_stub;
#define XInputSetState xinput_set_state

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(DirectSoundCreate_);

struct Win32OffscreenBuffer
{
    BITMAPINFO info;
    void *memory;
    int width;
    int height;

    int memorySize;
    int pitch;
};

struct Win32WindowDimension
{
    int width;
    int height;
};

struct Win32SoundOutput
{
    int16_t toneVolume = 4000;
    int toneHz = 256;
    int sampleHz = 48000;
    int bytesPerSample = sizeof(int16_t) * 2;
    int bufferSize = sampleHz * bytesPerSample;
    int targetCursorOffsetBytes =
        (bytesPerSample * sampleHz / 15); // 15 = min fps to not clip; increase for lower latency
    int wavePeriod = sampleHz / toneHz;
    uint32_t runningSampleIndex = 0;
    float t = 0;
};

// TODO: global for now
global bool gRunning = false;
global Win32OffscreenBuffer gBackBuffer;
global LPDIRECTSOUNDBUFFER gSecondaryBuffer;

internal void win32_load_xinput()
{
    HMODULE xInputLib = LoadLibraryA("xinput1_4.dll");

    if (!xInputLib)
    {
        xInputLib = LoadLibraryA("xinput9_1_0.dll");
    }

    if (!xInputLib)
    {
        xInputLib = LoadLibraryA("xinput1_3.dll");
    }

    // TODO: log loaded version

    if (xInputLib)
    {
        XInputGetState = (XInputGetState_ *)GetProcAddress(xInputLib, "XInputGetState");
        XInputSetState = (XInputSetState_ *)GetProcAddress(xInputLib, "XInputSetState");
    }
}

internal void win32_init_dsound(HWND window, DWORD sampleHz, DWORD bufferSize)
{
    HMODULE dSoundLib = LoadLibraryA("dsound.dll");

    if (dSoundLib)
    {
        DirectSoundCreate_ *DirectSoundCreate = (DirectSoundCreate_ *)GetProcAddress(dSoundLib, "DirectSoundCreate");
        LPDIRECTSOUND dSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &dSound, 0)))
        {
            WAVEFORMATEX waveFormat = {};
            waveFormat.wFormatTag = WAVE_FORMAT_PCM;
            waveFormat.nChannels = 2;
            waveFormat.nSamplesPerSec = sampleHz;
            waveFormat.wBitsPerSample = 16;
            waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
            waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
            waveFormat.cbSize = 0;

            if (SUCCEEDED(dSound->SetCooperativeLevel(window, DSSCL_PRIORITY)))
            {
                DSBUFFERDESC bufferDesc = {};
                bufferDesc.dwSize = sizeof(bufferDesc);
                bufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

                LPDIRECTSOUNDBUFFER primaryBuffer;
                if (SUCCEEDED(dSound->CreateSoundBuffer(&bufferDesc, &primaryBuffer, 0)))
                {
                    // primary buffer requires SetFormat
                    if (SUCCEEDED(primaryBuffer->SetFormat(&waveFormat)))
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

            DSBUFFERDESC bufferDesc = {};
            bufferDesc.dwSize = sizeof(bufferDesc);
            bufferDesc.dwFlags = 0;
            bufferDesc.dwBufferBytes = bufferSize;
            bufferDesc.lpwfxFormat = &waveFormat;

            if (SUCCEEDED(dSound->CreateSoundBuffer(&bufferDesc, &gSecondaryBuffer, 0)))
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

internal void win32_fill_sound_buffer(Win32SoundOutput *soundOutput, DWORD byteToLock, DWORD bytesToWrite)
{
    VOID *region1;
    VOID *region2;
    DWORD region1Size;
    DWORD region2Size;

    if (SUCCEEDED(gSecondaryBuffer->Lock(byteToLock, bytesToWrite, &region1, &region1Size, &region2, &region2Size, 0)))
    {
        int16_t *sampleOut = (int16_t *)region1;
        DWORD sampleCount = region1Size / soundOutput->bytesPerSample;
        for (DWORD sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++)
        {
            int16_t sampleValue = (int16_t)(sinf(soundOutput->t) * soundOutput->toneVolume);
            *sampleOut++ = sampleValue;
            *sampleOut++ = sampleValue;
            soundOutput->runningSampleIndex++;
            soundOutput->t += 2.0f * PI / (float)soundOutput->wavePeriod;
        }

        sampleOut = (int16_t *)region2;
        sampleCount = region2Size / soundOutput->bytesPerSample;
        for (DWORD sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++)
        {
            int16_t sampleValue = (int16_t)(sinf(soundOutput->t) * soundOutput->toneVolume);
            *sampleOut++ = sampleValue;
            *sampleOut++ = sampleValue;
            soundOutput->runningSampleIndex++;
            soundOutput->t += 2.0f * PI / (float)soundOutput->wavePeriod;
        }

        gSecondaryBuffer->Unlock(region1, region1Size, region2, region2Size);
    }
}

internal void win32_render_bitmap(Win32OffscreenBuffer *buffer, int xOffset, int yOffset)
{
    uint8_t *row = (uint8_t *)buffer->memory;

    for (int y = 0; y < buffer->height; y++)
    {
        uint32_t *pixel = (uint32_t *)row;

        for (int x = 0; x < buffer->width; x++)
        {
            uint8_t blue = x + xOffset;
            uint8_t green = y + yOffset;
            *pixel++ = (green << 8) | blue;
        }

        row += buffer->pitch;
    }
}

internal void win32_resize_dib_section(Win32OffscreenBuffer *buffer, int width, int height)
{
    if (buffer->memory)
    {
        VirtualFree(buffer->memory, 0, MEM_RELEASE);
    }

    buffer->width = width;
    buffer->height = height;
    buffer->pitch = width * BYTES_PER_PIXEL;
    buffer->memorySize = (width * height) * BYTES_PER_PIXEL;

    buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
    buffer->info.bmiHeader.biWidth = buffer->width;
    buffer->info.bmiHeader.biHeight = -buffer->height; // top-down
    buffer->info.bmiHeader.biPlanes = 1;
    buffer->info.bmiHeader.biBitCount = 32;
    buffer->info.bmiHeader.biCompression = BI_RGB;

    buffer->memory = VirtualAlloc(0, buffer->memorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    // TODO: clear to black
}

internal void win32_display_buffer(Win32OffscreenBuffer *buffer, HDC deviceCtx, int windowWidth, int windowHeight)
{
    // TODO: proper strech and aspect ratio
    StretchDIBits(deviceCtx, 0, 0, windowWidth, windowHeight, 0, 0, buffer->width, buffer->height, buffer->memory,
                  &buffer->info, DIB_RGB_COLORS, SRCCOPY);
}

internal Win32WindowDimension win32_get_window_dimension(HWND window)
{
    Win32WindowDimension ret;
    RECT clientRect;
    GetClientRect(window, &clientRect);
    ret.width = clientRect.right - clientRect.left;
    ret.height = clientRect.bottom - clientRect.top;
    return ret;
}

LRESULT CALLBACK win32_main_window_callback(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret = 0;

    switch (msg)
    {
    case WM_SIZE: {
        OutputDebugStringA("WM_SIZE\n");
    }
    break;

    case WM_CLOSE: {
        // TODO: handle with message to the user
        gRunning = false;
    }
    break;

    case WM_DESTROY: {
        // TODO: handle as error -> recreate window
        gRunning = false;
    }
    break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        uint32_t vkCode = wParam;
        bool wasDown = lParam & (1 << 30);
        bool isDown = !(lParam & (1 << 31));

        if (isDown == wasDown)
        {
            break;
        }

        switch (vkCode)
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
            if (lParam & (1 << 29)) // alt
            {
                gRunning = false;
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
        PAINTSTRUCT paint;
        HDC deviceCtx = BeginPaint(window, &paint);
        Win32WindowDimension dim = win32_get_window_dimension(window);
        win32_display_buffer(&gBackBuffer, deviceCtx, dim.width, dim.height);
        EndPaint(window, &paint);
    }
    break;

    default: {
        ret = DefWindowProcA(window, msg, wParam, lParam);
    }
    break;
    }

    return ret;
}

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCmd)
{
    win32_load_xinput();

    WNDCLASSA windowClass = {};

    win32_resize_dib_section(&gBackBuffer, 1280, 720);

    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = win32_main_window_callback;
    windowClass.hInstance = instance;
    // windowClass.hIcon; // TODO: game icon
    windowClass.lpszClassName = "HandmadeHeroWindowClass";

    if (RegisterClassA(&windowClass))
    {
        HWND window = CreateWindowExA(0, windowClass.lpszClassName, "Handmade Hero", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, instance, 0);

        if (window)
        {
            gRunning = true;

            int xOffset = 0;
            int yOffset = 0;

            Win32SoundOutput soundOutput;
            win32_init_dsound(window, soundOutput.sampleHz, soundOutput.bufferSize);
            win32_fill_sound_buffer(&soundOutput, 0, soundOutput.bufferSize);
            gSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);

            while (gRunning)
            {
                MSG msg;
                while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        gRunning = false;
                    }

                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }

                for (DWORD controllerIndex = 0; controllerIndex < XUSER_MAX_COUNT; controllerIndex++)
                {
                    XINPUT_STATE controllerState;
                    if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS)
                    {
                        // TODO: check if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD *pad = &controllerState.Gamepad;

                        bool up = pad->wButtons & XINPUT_GAMEPAD_DPAD_UP;
                        bool down = pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN;
                        bool left = pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT;
                        bool right = pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
                        bool a = pad->wButtons & XINPUT_GAMEPAD_A;
                        bool b = pad->wButtons & XINPUT_GAMEPAD_B;
                        bool x = pad->wButtons & XINPUT_GAMEPAD_X;
                        bool y = pad->wButtons & XINPUT_GAMEPAD_Y;
                        bool start = pad->wButtons & XINPUT_GAMEPAD_START;
                        bool back = pad->wButtons & XINPUT_GAMEPAD_BACK;
                        bool leftSholder = pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER;
                        bool rightSholder = pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER;

                        int16_t lStickX = pad->sThumbLX;
                        int16_t lStickY = pad->sThumbLY;

                        // TODO: handle deadzone with
                        // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE and
                        // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
                        xOffset += lStickX / 4096;
                        yOffset += lStickY / 4096;

                        if (a | b | x | y)
                        {
                            soundOutput.toneHz = a * 128 + b * 256 + x * 384 + y * 512;
                        }
                        else
                        {
                            soundOutput.toneHz = 256;
                        }

                        soundOutput.toneHz *= (1 + 0.5 * (float)lStickY / 30000.0f);
                        soundOutput.wavePeriod = soundOutput.sampleHz / soundOutput.toneHz;
                    }
                    else
                    {
                        // TODO: handle controller not available
                    }
                }

                win32_render_bitmap(&gBackBuffer, xOffset, yOffset);

                DWORD playCursor;
                DWORD writeCursor;

                if (SUCCEEDED(gSecondaryBuffer->GetCurrentPosition(&playCursor, &writeCursor)))
                {
                    DWORD bytesToWrite = 0;
                    DWORD byteToLock =
                        (soundOutput.runningSampleIndex * soundOutput.bytesPerSample) % soundOutput.bufferSize;

                    DWORD targetCursor = (playCursor + soundOutput.targetCursorOffsetBytes) % soundOutput.bufferSize;
                    if (byteToLock > targetCursor)
                    {
                        bytesToWrite = targetCursor + soundOutput.bufferSize - byteToLock;
                    }
                    else
                    {
                        bytesToWrite = targetCursor - byteToLock;
                    }

                    win32_fill_sound_buffer(&soundOutput, byteToLock, bytesToWrite);
                }

                HDC deviceCtx = GetDC(window);
                Win32WindowDimension dim = win32_get_window_dimension(window);
                win32_display_buffer(&gBackBuffer, deviceCtx, dim.width, dim.height);
                ReleaseDC(window, deviceCtx);
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
