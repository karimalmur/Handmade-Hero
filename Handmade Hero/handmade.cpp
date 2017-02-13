#include "handmade.h"

internal void DrawWeirdGradient(GameOffscreenBuffer* buffer, int xOffset, int yOffset)
{
	uint32* pixel = (uint32*)buffer->Memory;
	uint8* row = (uint8*)buffer->Memory;
	for (int y = 0; y < buffer->Height; y++)
	{
		pixel = (uint32*)row;
		for (int x = 0; x < buffer->Width; x++)
		{
			uint8 blue = (uint8)(x + yOffset);
			uint8 green = (uint8)(y + xOffset);
			*pixel++ = (uint32)((green << 8) | blue);
		}
		row += buffer->Pitch;
	}
}

internal void GameOutputSound (GameSoundOutputBuffer* soundBuffer, int toneHz)
{
	int16* sampleOut = (int16*)soundBuffer->Samples;
	local_persist real32 tSine;
	int wavePeriod = soundBuffer->SamplesPerSecond / toneHz;
	int16 toneVolume = 1200;

	for (int sampleIndex = 0; sampleIndex < soundBuffer->SampleCount; sampleIndex++)
	{
		real32 sineValue = sinf(tSine);
		int16 sampleValue = (int16)(sineValue * toneVolume);
		*sampleOut++ = sampleValue;
		*sampleOut++ = sampleValue;
		tSine += (2.0f * PI32) / (real32)wavePeriod;
	}
}

void GameUpdateAndRender(GameMemory* memory, GameOffscreenBuffer* buffer, GameSoundOutputBuffer* soundBuffer,
						 GameInput* input)
{
	GameState* gameState = (GameState*)memory->PermenantStorage;
	Assert(sizeof(gameState) <= memory->PermenantStorageSize);

	char* fileName = __FILE__;
	DebugReadFileResult file = DEBUGPlatformReadEntireFile(fileName);
	if (file.Content)
	{
		DEBUGPlatformWriteFile("LOL.out", file.Content, file.ContentSize);
		DEBUGPlatformFreeFileMemory(file.Content);
	}

	if (!memory->IsInitialized)
	{
		gameState->ToneHz = 256;

		memory->IsInitialized = true;
	}
	GameControllerInput input0 = input->Controllers[0];
	if (input0.IsAnalog)
	{
		//use analog movement tuning
		gameState->YOffset += (int)(4.0f*(input0.EndX));
		gameState->ToneHz += 256 + (int)(128.0f*(input0.EndY));
	}
	else
	{
		//use digital movement tuning
	}
	if (input0.Down.EnddedDown)
	{
		gameState->XOffset++;
	}	

	GameOutputSound(soundBuffer, gameState->ToneHz);
	DrawWeirdGradient(buffer, gameState->XOffset, gameState->YOffset);
}