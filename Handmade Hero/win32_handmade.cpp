// TODO: THIS IS NOT A FINAL PLATFORM LAYER!!
/**
 *   - Saved game locations
 *   - Getting a handle to our own executable file
 *   - Asset loading path
 *   - Threading (launch a thread)
 *   - Raw Input (support for multiple keyboards)
 *   - Sleep/timeBeginPeriod
 *   - ClipCursor() (multimonitor support)
 *   - Fullscreen support
 *   - WM_SETCURSOR (control cursor visibility)
 *   - QueryCanelAutoplay
 *   - WM_ACTIVATEAPP (for when we are not active application)
 *   - Blit speed improvemetns (BitBlt)
 *   - Hardware acceleration (OpenGL or Direct3D or BOTH??)
 *   - GetKeyboardLayout (for French keybords, international WASD support)
 *
 *   Just a partial list of stuff!!
 */
#include "windows.h"
#include "xinput.h"
#include "dsound.h"
#include "stdio.h"
#include "util.h"

#include "win32_handmade.h"
#include "handmade.h"

global_variable bool GlobalRunning;
global_variable Win32OffscreenBuffer GlobalBackbuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSoundBuffer; // The actual sound buffer that we will write to.
global_variable Win32SoundOutput GlobalSoundOutput;
global_variable int64 GlobalPerfCountFrequency;

//Support for xInputGetState and xInputSetState without having to reference xinput.dll because
// many windows installations (especially win 7) doesnt have it by default
//We are creating a function pointer to point to xInputGetState and xInputSetState, and having
// these func pointers point to a stub func in case xinput1_4.dll doesnt exist and using the
// game pad is not possible. Otherwise, if we actually referenced that lib and it didnt exist,
// then the game will crash even though want the user t be able to play using the keyboard.
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(X_Input_Get_State);
#define XInputGetState XInputGetState_ //to not collide with the original XInputGetState in xinput.dll, this way when we refer to XInputGetState
									   // we are referencing our own XInputGetState_

X_INPUT_GET_STATE(XInputGetStateStub)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

global_variable X_Input_Get_State* XInputGetState_ = XInputGetStateStub;

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(X_Input_Set_State);
#define XInputSetState XInputSetState_

X_INPUT_SET_STATE(XInputSetStateStub)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

global_variable X_Input_Set_State* XInputSetState_ = XInputSetStateStub;

internal void Win32LoadXInput()
{
	HMODULE xInputLibrary = LoadLibraryA("xinput1_4.dll");
	if (!xInputLibrary)
	{
		xInputLibrary = LoadLibraryA("xinput9_1_0.dll");
	}
	if (!xInputLibrary)
	{
		xInputLibrary = LoadLibraryA("xinput1_3.dll");
	}
	if (xInputLibrary)
	{
		XInputGetState = (X_Input_Get_State*) GetProcAddress(xInputLibrary, "XInputGetState");
		if (!XInputGetState)
			XInputGetState = XInputGetStateStub;
		XInputSetState = (X_Input_Set_State*) GetProcAddress(xInputLibrary, "XInputSetState");
		if (!XInputSetState)
			XInputSetState = XInputSetStateStub;
	}
}

DebugReadFileResult DEBUGPlatformReadEntireFile(char* fileName)
{
	DebugReadFileResult result = {};
	HANDLE fileHandle = CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (fileHandle != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER fileSize;
		if (GetFileSizeEx(fileHandle, &fileSize))
		{
			//We are neve going to read a file bigger than 4gigs.
			uint32 fileSize32 = SafeTruncateUInt64(fileSize.QuadPart);
			result.Content = VirtualAlloc(0, (SIZE_T)fileSize.QuadPart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (result.Content)
			{
				DWORD bytesRead;
				if (ReadFile(fileHandle, result.Content, fileSize32, &bytesRead, 0) 
					&& fileSize32 == bytesRead)
				{
					result.ContentSize = fileSize32;
				}
				else
				{
					DEBUGPlatformFreeFileMemory(result.Content);
					result.Content = 0;
				}
			}
		}
		CloseHandle(fileHandle);
	}
	return result;
}

void DEBUGPlatformFreeFileMemory(void* memory)
{
	VirtualFree(memory, 0, MEM_RELEASE);
}

bool32 DEBUGPlatformWriteFile(char* fileName, void* memory, uint32 memorySize)
{
	bool32 result = false;
	HANDLE fileHandle = CreateFile(fileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	if (fileHandle != INVALID_HANDLE_VALUE)
	{
		DWORD bytesWritten;
		if (WriteFile(fileHandle, memory, memorySize, &bytesWritten, 0))
		{
			result = bytesWritten == memorySize;
		}
		else
		{
					
		}
		CloseHandle(fileHandle);
	}
	else
	{

	}
	return result;
}

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(_In_opt_ LPCGUID pcGuidDevice, _Outptr_ LPDIRECTSOUND* ppDS, _Pre_null_ LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(Direct_Sound_Create);

internal void Win32InitDSound(HWND window, int32 samplesPerSecond, int32 bufferSize)
{
	// Load the Libray: while allowing the game to run if we were not able to load it.
	HMODULE dSoundLibrary = LoadLibrary("dsound.dll");
	if (dSoundLibrary)
	{
		// Get a DirectSound object
		Direct_Sound_Create* directSoundCreate = (Direct_Sound_Create*)GetProcAddress(dSoundLibrary, "DirectSoundCreate");
		LPDIRECTSOUND directSound;
		if (directSoundCreate && SUCCEEDED(directSoundCreate(0, &directSound, 0)))
		{
			WAVEFORMATEX waveFormat = {};
			waveFormat.cbSize = 0;
			waveFormat.nChannels = 2;
			waveFormat.nSamplesPerSec = samplesPerSecond;
			waveFormat.wBitsPerSample = 16;
			waveFormat.wFormatTag = WAVE_FORMAT_PCM;
			waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
			waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

			if (SUCCEEDED(directSound->SetCooperativeLevel(window, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC bufferDescription = {};
				bufferDescription.dwSize = sizeof(bufferDescription);
				bufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
				// Create a primary buffer (it just sets the mode of the sound card)
				LPDIRECTSOUNDBUFFER primaryBuffer;
				if (SUCCEEDED(directSound->CreateSoundBuffer(&bufferDescription, &primaryBuffer, 0)))
				{
					if (SUCCEEDED(primaryBuffer->SetFormat(&waveFormat)))
					{
						OutputDebugString("Primary buffer format was set.\n");
					}
					else
					{

					}
				}
			}
			else
			{

			}
			// Create a secondary buffer which we will actually be writing to
			DSBUFFERDESC bufferDescription = {};
			bufferDescription.dwSize = sizeof(bufferDescription);
			bufferDescription.dwFlags = 0;
			bufferDescription.dwBufferBytes = bufferSize;
			bufferDescription.lpwfxFormat = &waveFormat;
			HRESULT Error = directSound->CreateSoundBuffer(&bufferDescription, &GlobalSoundBuffer, 0);
			if (SUCCEEDED(Error)) {
				OutputDebugString("Secondary buffer was created.\n");
			}
			else {
			}
		}
	}
}

internal Win32WindowDimension Win32GetWindowDimension(HWND window)
{
	Win32WindowDimension result;
	RECT clientRect;
	GetClientRect(window, &clientRect);
	result.Width = clientRect.right - clientRect.left;
	result.Height = clientRect.bottom - clientRect.top;
	return result;
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
	buffer->Memory = VirtualAlloc(NULL, bitmapMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	buffer->Pitch = buffer->Width * bytesPerPixel;
}

internal void Win32DisplayBufferInWindow(HDC deviceContext, Win32OffscreenBuffer* buffer, int windowWidth, int windowHeight)
{
	StretchDIBits(deviceContext,
		0, 0, windowWidth, windowHeight //destination
		, 0, 0, buffer->Width, buffer->Height, //source
		buffer->Memory,	&(buffer->Info), DIB_RGB_COLORS, SRCCOPY);
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
	case WM_SYSKEYUP:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_KEYDOWN:
	{
		Assert("Keyboard input came through a non-dispatch message!");
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
		Win32DisplayBufferInWindow(deviceContext, &GlobalBackbuffer, windowDimension.Width, windowDimension.Height);
		EndPaint(window, &paint);
	}break;
	default:
	{
		result = DefWindowProc(window, message, wParam, lParam);
	}break;
	}
	return result;
}

internal void Win32ClearSoundBuffer(Win32SoundOutput* soundOutput)
{
	VOID* region1;
	DWORD region1Size;
	VOID* region2;
	DWORD region2Size;
	if (SUCCEEDED(GlobalSoundBuffer->Lock(0, soundOutput->SoundBufferSize,
		&region1, &region1Size,
		&region2, &region2Size, 0)))
	{
		uint8* destSample = (uint8*)region1;
		for (DWORD byteIndex = 0; byteIndex < region1Size; byteIndex++)
		{
			*destSample++ = 0;
		}

		destSample = (uint8*)region2;
		for (DWORD byteIndex = 0; byteIndex < region2Size; byteIndex++)
		{
			*destSample++ = 0;
		}
		GlobalSoundBuffer->Unlock(region1, region1Size, region2, region2Size);
	}
}

internal void Win32FillSoundBuffer(Win32SoundOutput* soundOutput, DWORD byteToLock, DWORD bytesToWrite,
	GameSoundOutputBuffer* sourceBuffer)
{
	VOID* region1;
	DWORD region1Size;
	VOID* region2;
	DWORD region2Size;

	if (SUCCEEDED(GlobalSoundBuffer->Lock(byteToLock, bytesToWrite,
											&region1, &region1Size,
											&region2, &region2Size, 0)))
	{
		DWORD region1SampleCount = region1Size / soundOutput->BytesPerSample;
		int16* destSample = (int16*)region1;
		int16* sourceSample = sourceBuffer->Samples;
		for (DWORD sampleIndex = 0; sampleIndex < region1SampleCount; sampleIndex++)
		{
			*destSample++ = *sourceSample++;
			*destSample++ = *sourceSample++;
			soundOutput->RunningSampleIndex++;
		}

		DWORD region2SampleCount = region2Size / soundOutput->BytesPerSample;
		destSample = (int16*)region2;
		for (DWORD sampleIndex = 0; sampleIndex < region2SampleCount; sampleIndex++)
		{
			*destSample++ = *sourceSample++;
			*destSample++ = *sourceSample++;
			soundOutput->RunningSampleIndex++;
		}
		GlobalSoundBuffer->Unlock(region1, region1Size, region2, region2Size);
	}
}

internal void Win32ProcessXInputDigitalButtons(WORD xInputButtonState, GameButtonState* oldState, DWORD buttonBit, GameButtonState* newState)
{
	newState->EnddedDown = (buttonBit & xInputButtonState) == buttonBit;
	newState->HalfTransitionCount = (oldState->EnddedDown != newState->EnddedDown) ? 1 : 0;
}

internal void Win32ProcessKeyboardMessage(GameButtonState* newState, bool32 isDown)
{
	// We are only counting half transitions in a single frame 
	//(like the user in the duration of one frame released a button,
	//pressed it then released it  again. We are not counting when the user
	//is doing a long press that spans the duration of a frame or many frames.
	// This is ensured by the (wasDown != isDown) check.
	Assert(newState->EnddedDown != isDown);
	newState->EnddedDown = isDown;
	newState->HalfTransitionCount++;
}

internal void Win32ProcessPendingMessages(GameControllerInput* keyboardController)
{
	MSG message;
	while (PeekMessage(&message, 0, 0, 0, PM_REMOVE))
	{
		switch (message.message)
		{
		case WM_QUIT:
		{
			GlobalRunning = false;
		} break;
		case WM_SYSKEYUP:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_KEYDOWN:
		{
			WPARAM vkCode = message.wParam;
			bool32 wasDown = ((message.lParam & (1 << 30)) != 0);
			bool32 isDown = ((message.lParam & (1 << 31)) == 0);
			if (wasDown != isDown)
			{
				if (vkCode == VK_F4)
				{
					bool altDown = ((1 << 29) & message.lParam) != 0;
					if (altDown)
						GlobalRunning = false;
				}
				else if (vkCode == 'A')
				{
					Win32ProcessKeyboardMessage(&keyboardController->MoveLeft, isDown);
				}
				else if (vkCode == 'S')
				{
					Win32ProcessKeyboardMessage(&keyboardController->MoveDown, isDown);
				}
				else if (vkCode == 'D')
				{
					Win32ProcessKeyboardMessage(&keyboardController->MoveRight, isDown);
				}
				else if (vkCode == 'W')
				{
					Win32ProcessKeyboardMessage(&keyboardController->MoveUp, isDown);
				}
				else if (vkCode == 'Q')
				{
					Win32ProcessKeyboardMessage(&keyboardController->LeftShoulder, isDown);
				}
				else if (vkCode == 'E')
				{
					Win32ProcessKeyboardMessage(&keyboardController->RightShoulder, isDown);
				}
				else if (vkCode == VK_UP)
				{
					Win32ProcessKeyboardMessage(&keyboardController->ActionUp, isDown);
				}
				else if (vkCode == VK_DOWN)
				{
					Win32ProcessKeyboardMessage(&keyboardController->ActionDown, isDown);
				}
				else if (vkCode == VK_LEFT)
				{
					Win32ProcessKeyboardMessage(&keyboardController->ActionLeft, isDown);
				}
				else if (vkCode == VK_RIGHT)
				{
					Win32ProcessKeyboardMessage(&keyboardController->ActionRight, isDown);
				}
				else if (vkCode == VK_ESCAPE)
				{
					GlobalRunning = false;
				}
				else if (vkCode == VK_SPACE)
				{

				}
			}
		} break;
		default:
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		} break;
		}
	}
}

internal real32 Win32ProcessXInputStickValue(SHORT thumbValue, int deadZoneThreshold)
{
	real32 result = 0;
	if (thumbValue < -deadZoneThreshold)
	{
		result = (real32)(thumbValue + deadZoneThreshold) / 
			(32768 - deadZoneThreshold);
	}
	else if (thumbValue > deadZoneThreshold)
	{
		result = (real32)(thumbValue - deadZoneThreshold) /
			(32767 - deadZoneThreshold);
	}
	return result;
}

inline LARGE_INTEGER Win32GetWallClock()
{
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result;

}

inline real32 Win32GetSecondsElapsed(LARGE_INTEGER start, LARGE_INTEGER end)
{
	real32  secondsElapsedForWork = ((real32)(end.QuadPart - start.QuadPart) /
									 (real32)GlobalPerfCountFrequency);
	return secondsElapsedForWork;
}

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow
)
{
	LARGE_INTEGER perfFrequencyResult;
	QueryPerformanceFrequency(&perfFrequencyResult);
	GlobalPerfCountFrequency = perfFrequencyResult.QuadPart;

	// Set the windows scheduler granularity to 1ms so that our sleep can be more granular.
	uint32 desiredSchudlerMs = 1;
	bool32 sleepIsGranular = timeBeginPeriod(desiredSchudlerMs) == TIMERR_NOERROR;

	Win32LoadXInput();
	Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

	WNDCLASS windowClass = {}; //init all struct members to 0
	windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = MainWindowCallback;//callback from windows when our window needs to do somwthing
	windowClass.hInstance = hInstance;
	windowClass.lpszClassName = "HandmadeHeroWindowClass";

	int monitorRefreshHz = 60;
	int gameUpdateHz = monitorRefreshHz / 2;
	real32 targetSecondsPerFrame = 1.0f / (real32)gameUpdateHz;

	if (RegisterClass(&windowClass))
	{
		HWND window = CreateWindowEx(0, windowClass.lpszClassName, "Handmade Hero",
			WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hInstance, 0);
		if (window)
		{
			GlobalSoundOutput.SamplesPerSecond = 48000;
			GlobalSoundOutput.BytesPerSample = sizeof(int16) * 2;
			GlobalSoundOutput.RunningSampleIndex = 0;
			GlobalSoundOutput.SoundBufferSize = GlobalSoundOutput.SamplesPerSecond * GlobalSoundOutput.BytesPerSample;
			GlobalSoundOutput.LatencySampleCount = GlobalSoundOutput.SamplesPerSecond / 15;
			Win32InitDSound(window, GlobalSoundOutput.SamplesPerSecond, GlobalSoundOutput.SoundBufferSize);
			Win32ClearSoundBuffer(&GlobalSoundOutput);
			GlobalSoundBuffer->Play(0, 0, DSBPLAY_LOOPING);

			int16* soundSamples = (int16*)VirtualAlloc(NULL, GlobalSoundOutput.SoundBufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

			HDC deviceContext = GetDC(window);

			GlobalRunning = true;

#if HANDMADE_INTERNAL
			LPVOID baseAddress = (LPVOID)Terabytes(2);
#else
			LPVOID baseAddress = 0;
#endif
			GameMemory gameMemory = {};
			gameMemory.PermenantStorageSize = Megabytes(64);
			gameMemory.TransientStorageSize = Gigabytes(1);
			uint64 totalSize = gameMemory.PermenantStorageSize + gameMemory.TransientStorageSize;
			gameMemory.PermenantStorage = VirtualAlloc(baseAddress, (SIZE_T)totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			gameMemory.TransientStorage = (uint8*)gameMemory.PermenantStorage + gameMemory.PermenantStorageSize;

			if (gameMemory.PermenantStorage && gameMemory.TransientStorage && soundSamples)
			{
				GameInput input[2] = {};
				GameInput* oldInput = &input[0];
				GameInput* newInput = &input[1];

				LARGE_INTEGER lastCounter = Win32GetWallClock();

				uint64 lastCycleCount = __rdtsc();
				while (GlobalRunning)
				{
					GameControllerInput* oldKeyboardController = GetController(oldInput, 0);
					GameControllerInput* newKeyboardController = GetController(newInput, 0);
					*newKeyboardController = {};
					newKeyboardController->IsConnected = true;
					for (int buttonIndex = 0; buttonIndex < ArrayCount(newKeyboardController->Buttons); buttonIndex++)
					{
						newKeyboardController->Buttons[buttonIndex].EnddedDown = 
							oldKeyboardController->Buttons[buttonIndex].EnddedDown;
					}
					Win32ProcessPendingMessages(newKeyboardController);
					
					DWORD maxControllerCount = XUSER_MAX_COUNT;
					if (maxControllerCount > ArrayCount(newInput->Controllers) - 1)
					{
						maxControllerCount = ArrayCount(newInput->Controllers) - 1;
					}
					//polling for controller input
					for (DWORD controllerIndex = 0; controllerIndex < maxControllerCount; controllerIndex++)
					{
						DWORD ourControllerIndex = controllerIndex + 1; // cause of the keyboard.
						GameControllerInput* oldController = GetController(oldInput, ourControllerIndex);
						GameControllerInput* newController = GetController(newInput, ourControllerIndex);

						XINPUT_STATE controllerState;
						if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS)
						{
							//ERROR_SUCCESS: controller is plugged in
							newController->IsConnected = true;
							newController->IsAnalog = true;
							XINPUT_GAMEPAD *pad = &controllerState.Gamepad;
							bool32 up = XINPUT_GAMEPAD_DPAD_UP & pad->wButtons;
							bool32 down = XINPUT_GAMEPAD_DPAD_DOWN & pad->wButtons;
							bool32 left = XINPUT_GAMEPAD_DPAD_LEFT & pad->wButtons;
							bool32 right = XINPUT_GAMEPAD_DPAD_RIGHT & pad->wButtons;

							newController->StickAverageX = Win32ProcessXInputStickValue(pad->sThumbLX
								, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
							newController->StickAverageY = Win32ProcessXInputStickValue(pad->sThumbLY
								, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

							if (newController->StickAverageX != 0.0f || newController->StickAverageY != 0.0f)
							{
								newController->IsAnalog = true;
							}

							if (up)
							{
								newController->StickAverageY = 1.0f;
								newController->IsAnalog = false;
							}
							if (down)
							{
								newController->StickAverageY = -1.0f;
								newController->IsAnalog = false;
							}
							if (left)
							{
								newController->StickAverageX = -1.0f;
								newController->IsAnalog = false;
							}
							if (right)
							{
								newController->StickAverageX = 1.0f;
								newController->IsAnalog = false;
							}

							real32 threshold = 0.5f;
							Win32ProcessXInputDigitalButtons(newController->StickAverageX < -threshold ? 1 : 0,
								&oldController->MoveLeft, 1, &newController->MoveLeft);
							Win32ProcessXInputDigitalButtons(newController->StickAverageX > threshold ? 1 : 0,
								&oldController->MoveRight, 1, &newController->MoveRight);
							Win32ProcessXInputDigitalButtons(newController->StickAverageY < -threshold ? 1 : 0,
								&oldController->MoveDown, 1, &newController->MoveDown);
							Win32ProcessXInputDigitalButtons(newController->StickAverageY > threshold ? 1 : 0,
								&oldController->MoveUp, 1, &newController->MoveUp);

							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->ActionDown
								, XINPUT_GAMEPAD_A, &newController->ActionDown);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->ActionRight
								, XINPUT_GAMEPAD_B, &newController->ActionRight);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->ActionLeft
								, XINPUT_GAMEPAD_X, &newController->ActionLeft);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->ActionUp
								, XINPUT_GAMEPAD_Y, &newController->ActionUp);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->RightShoulder
								, XINPUT_GAMEPAD_RIGHT_SHOULDER, &newController->RightShoulder);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->LeftShoulder
								, XINPUT_GAMEPAD_LEFT_SHOULDER, &newController->LeftShoulder);

							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->Back
								, XINPUT_GAMEPAD_LEFT_SHOULDER, &newController->Back);
							Win32ProcessXInputDigitalButtons(pad->wButtons, &oldController->Start
								, XINPUT_GAMEPAD_LEFT_SHOULDER, &newController->Start);
						}
						else
						{
							newController->IsConnected = false;
						}
					}

					DWORD playCursor;
					DWORD writeCursor;
					DWORD byteToLock;
					DWORD bytesToWrite;
					DWORD targetCursor;
					bool32 soundIsValid = false;

					if (SUCCEEDED(GlobalSoundBuffer->GetCurrentPosition(&playCursor, &writeCursor)))
					{
						byteToLock = (GlobalSoundOutput.RunningSampleIndex * GlobalSoundOutput.BytesPerSample) % GlobalSoundOutput.SoundBufferSize;
						targetCursor = (playCursor + GlobalSoundOutput.LatencySampleCount * GlobalSoundOutput.BytesPerSample)
							% GlobalSoundOutput.SoundBufferSize;
						if (byteToLock > targetCursor)
						{
							bytesToWrite = GlobalSoundOutput.SoundBufferSize - byteToLock;
							bytesToWrite += targetCursor;
						}
						else
						{
							bytesToWrite = targetCursor - byteToLock;
						}
						soundIsValid = true;
					}

					GameSoundOutputBuffer soundBuffer = {};
					soundBuffer.SamplesPerSecond = GlobalSoundOutput.SamplesPerSecond;
					soundBuffer.SampleCount = bytesToWrite / GlobalSoundOutput.BytesPerSample; // We are targeting 30 frames per second for now.
					soundBuffer.Samples = soundSamples;

					GameOffscreenBuffer buffer = {};
					buffer.Memory = GlobalBackbuffer.Memory;
					buffer.Width = GlobalBackbuffer.Width;
					buffer.Height = GlobalBackbuffer.Height;
					buffer.Pitch = GlobalBackbuffer.Pitch;
					GameUpdateAndRender(&gameMemory, &buffer, &soundBuffer, newInput);


					if (soundIsValid)
					{
						Win32FillSoundBuffer(&GlobalSoundOutput, byteToLock, bytesToWrite, &soundBuffer);
					}



					LARGE_INTEGER workCounter = Win32GetWallClock();					
					real32 workSecondsElapsed = Win32GetSecondsElapsed(lastCounter, workCounter);

					real32 secondsElapsedForFrame = workSecondsElapsed;
					if (secondsElapsedForFrame < targetSecondsPerFrame)
					{
						if(sleepIsGranular)
						{
							DWORD sleepMs = (DWORD)(1000.0f * (targetSecondsPerFrame - secondsElapsedForFrame));
							if(sleepMs > 0)
								Sleep(sleepMs);
							/*Assert(Win32GetSecondsElapsed(lastCounter,
								Win32GetWallClock()) > targetSecondsPerFrame);*/
						}
						while (secondsElapsedForFrame < targetSecondsPerFrame)
						{
							secondsElapsedForFrame = Win32GetSecondsElapsed(lastCounter,
								Win32GetWallClock());
						}
					}
					else
					{
						// TODO: Log miss of frame rate.
					}
					
					Win32WindowDimension windowDimension = Win32GetWindowDimension(window);
					Win32DisplayBufferInWindow(deviceContext, &GlobalBackbuffer, windowDimension.Width, windowDimension.Height);

					
					GameInput* temp = newInput;
					newInput = oldInput;
					oldInput = temp;

					LARGE_INTEGER endCounter = Win32GetWallClock();
					real32 msPerFrame = (1000.f * Win32GetSecondsElapsed(lastCounter, endCounter));
					real32 fps = 0.0f;
					lastCounter = endCounter;
					
					uint64 endCycleCount = __rdtsc();
					int64 cyclesElapsed = endCycleCount - lastCycleCount;
					lastCycleCount = endCycleCount;
					real32 megaCyclesPerFrame = (real32)(cyclesElapsed / (1000 * 1000));



					char stringBuffer[256];
					sprintf_s(stringBuffer, "%fmsf, %ffps, %fmcpf\n",
						msPerFrame, fps, megaCyclesPerFrame);
					OutputDebugString(stringBuffer);
					
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
	}
	else
	{
		
	}
	return 0;
}