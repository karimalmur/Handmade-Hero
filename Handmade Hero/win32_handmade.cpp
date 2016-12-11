#include "windows.h"
#include "stdint.h"

typedef uint8_t uint8;
typedef uint8_t uint16;
typedef uint32_t uint32;
typedef uint32_t uint64;

#define internal static
#define local_persist static
#define global_variable static

struct Win32OffscreenBuffer
{
	int Width;
	int Height;
	BITMAPINFO Info;
	void* Memory;
	int Pitch;
};

global_variable bool GlobalRunning;
global_variable Win32OffscreenBuffer GlobalBackbuffer;

struct Win32WindowDimension
{
	int Width;
	int Height;
};

internal Win32WindowDimension Win32GetWindowDimension(HWND window)
{
	Win32WindowDimension result;
	RECT clientRect;
	GetClientRect(window, &clientRect);
	result.Width = clientRect.right - clientRect.left;
	result.Height = clientRect.bottom - clientRect.top;
	return result;
}

internal void DrawWieredGradient(Win32OffscreenBuffer buffer, int xOffset, int yOffset)
{
	uint32* pixel = (uint32*)buffer.Memory;
	uint8* row = (uint8*)buffer.Memory;
	for (int y = 0; y < buffer.Height; y++)
	{
		pixel = (uint32*)row;
		for (int x = 0; x < buffer.Width; x++)
		{
			uint8 blue = x + xOffset;
			uint8 green = y + yOffset;
			*pixel++ = (uint32)((green << 8) | blue);
		}
		row += buffer.Pitch;
	}
}

internal void Win32ResizeDIBSection(Win32OffscreenBuffer *buffer, int width, int height)
{
	if (buffer->Memory)
	{
		VirtualFree(buffer->Memory, 0, MEM_RELEASE);
	}

	buffer->Width = width;
	buffer->Height = height;

	buffer->Info.bmiHeader.biSize = sizeof(buffer->Info.bmiHeader);
	buffer->Info.bmiHeader.biWidth = buffer->Width;
	//Set the biHeight to negative BitmapHeight so the bitmap is considered a top-down DIB and its origin is the upper-left corner.
	//See https://msdn.microsoft.com/en-us/library/windows/desktop/dd183376(v=vs.85).aspx
	buffer->Info.bmiHeader.biHeight = -buffer->Height;
	buffer->Info.bmiHeader.biPlanes = 1;
	buffer->Info.bmiHeader.biBitCount = 32; //RGB is just 24, but will ask for 32 for D-Word Alignment (cpu accesses memory at multiple of 4 easier :) )
	buffer->Info.bmiHeader.biCompression = BI_RGB;

	int bytesPerPixel = 4;
	int	bitmapMemorySize = bytesPerPixel * buffer->Width * buffer->Height;
	buffer->Memory = VirtualAlloc(NULL, bitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
	buffer->Pitch = buffer->Width * bytesPerPixel;
	DrawWieredGradient(*buffer, 0, 0);
}

internal void Win32DisplayBufferInWindow(HDC deviceContext, Win32OffscreenBuffer buffer, int windowWidth, int windowHeight)
{
	//Passed the whole rect and not a pointer to it. Maybe more efficent as this function is small and might be inlined anyway
	// and the compiler will use the same Rect that is already on the stack.
	StretchDIBits(deviceContext,
		0, 0, windowWidth, windowHeight //destination
		, 0, 0, buffer.Width, buffer.Height, //source
		buffer.Memory,
		&(buffer.Info), DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK MainWindowCallback(HWND window, UINT message
						, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (message)
	{
	case WM_SIZE:
	{
	}break;
	case WM_DESTROY:
	{
		OutputDebugString("WM_DESTORY\n");
		GlobalRunning = false;
	}break;
	case WM_CLOSE:
	{
		OutputDebugString("WM_CLOSE\n");
		GlobalRunning = false;
	}break;
	case WM_ACTIVATEAPP:
	{
		OutputDebugString("WM_ACTIVATEAPP\n");
	}break;
	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC deviceContext = BeginPaint(window, &paint);
		Win32WindowDimension windowDimension = Win32GetWindowDimension(window);
		Win32DisplayBufferInWindow(deviceContext, GlobalBackbuffer, windowDimension.Width, windowDimension.Height);
		EndPaint(window, &paint);
	}break;
	default:
	{
		result = DefWindowProc(window, message, wParam, lParam);
	}break;
	}
	return result;
}

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow
)
{
	Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

	WNDCLASS windowClass = {}; //init all struct members to 0
	windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = MainWindowCallback;//callback from windows when our window needs to do somwthing
	windowClass.hInstance = hInstance;
	windowClass.lpszClassName = "HandmadeHeroWindowClass";

	if (RegisterClass(&windowClass))
	{
		HWND window = CreateWindowEx(0, windowClass.lpszClassName, "Handmade Hero",
			WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hInstance, 0);
		if (window)
		{
			GlobalRunning = true;
			HDC deviceContext = GetDC(window);
			int xOffset = 0;
			while (GlobalRunning)
			{
				MSG message; //define it inside the loop, it will not make any difference to the compiler 
							 // + u get the benefit of not beign able to mistakenly reference it outside the loop.
							 //Unless of course the type is a class that has a destructor that u dont want to be called at
							 // every iteration.
				while (PeekMessage(&message, 0, 0, 0, PM_REMOVE))
				{
					if (message.message == WM_QUIT)
						GlobalRunning = false;
					TranslateMessage(&message);
					DispatchMessage(&message);
				}
				DrawWieredGradient(GlobalBackbuffer, xOffset, 0);
				Win32WindowDimension windowDimension = Win32GetWindowDimension(window);
				Win32DisplayBufferInWindow(deviceContext, GlobalBackbuffer, windowDimension.Width, windowDimension.Height);
				xOffset++;
			}
			//We specified CS_OWNDC => we are telling windows that we want our own DC that we wont have to return to DC pool
			// => there is no need to Release it.
		}
		else
		{

		}
	}
	else
	{
		
	}
	return 0;
}