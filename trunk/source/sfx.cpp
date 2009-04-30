#include "sfx.h"
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>
using namespace std;

#ifdef _WIN32
	#ifndef _XBOX
		#pragma comment(lib, "SDL_mixer.lib")
	#endif
#endif

extern bool fResumeMusic;
extern void DECLSPEC soundfinished(int channel);
extern void DECLSPEC musicfinished();

extern sfxSound * g_PlayingSoundChannels[NUM_SOUND_CHANNELS];
static Uint8 *musicBuffer;
static SDL_RWops *musicRW;

bool sfx_init()
{
	Mix_OpenAudio(48000, AUDIO_S16MSB, 2, 2048);
	Mix_AllocateChannels(NUM_SOUND_CHANNELS);

	for(short iChannel = 0; iChannel < NUM_SOUND_CHANNELS; iChannel++)
		g_PlayingSoundChannels[iChannel] = NULL;

    return true;
}

void sfx_close()
{
	Mix_CloseAudio();
}

void sfx_stopallsounds()
{
	Mix_HaltChannel(-1);

	for(short iChannel = 0; iChannel < NUM_SOUND_CHANNELS; iChannel++)
		g_PlayingSoundChannels[iChannel] = NULL;
}

void sfx_setmusicvolume(int volume)
{
	Mix_VolumeMusic(volume);
}

void sfx_setsoundvolume(int volume)
{
	Mix_Volume(-1, volume);
}

sfxSound::sfxSound()
{
	paused = false;
	ready = false;
	sfx = NULL;
}

sfxSound::~sfxSound()
{}

bool sfxSound::init(const string& filename)
{
	if(sfx)
		reset();

	cout << "load " << filename << "..." << endl;
	sfx = Mix_LoadWAV(filename.c_str());

	if(sfx == NULL)
	{
		printf(" failed!\n");
		return false;
	}

	channel = -1;
	starttime = 0;
	ready = true;
	instances = 0;

	Mix_ChannelFinished(&soundfinished);

	return true;
}

int sfxSound::play()
{
	int ticks = SDL_GetTicks();

	//Don't play sounds right over the top (doubles volume)
	if(channel < 0 || ticks - starttime > 40)
	{
		instances++;
		channel = Mix_PlayChannel(-1, sfx, 0);

		if(channel < 0)
			return channel;

		starttime = ticks;

		if(g_PlayingSoundChannels[channel])
			printf("Error: Sound was played on channel that was not cleared!\n");

		g_PlayingSoundChannels[channel] = this;
	}
	return channel;
}

int sfxSound::playloop(int iLoop)
{
	instances++;
	channel = Mix_PlayChannel(-1, sfx, iLoop);

	if(channel < 0)
		return channel;

	g_PlayingSoundChannels[channel] = this;

	return channel;
}

void sfxSound::stop()
{
	if(channel != -1)
	{
		instances = 0;
		Mix_HaltChannel(channel);
		channel = -1;
	}
}

void sfxSound::sfx_pause()
{
	paused = !paused;

	if(paused)
		Mix_Pause(channel);
	else
		Mix_Resume(channel);
}

void sfxSound::clearchannel()
{
	if(--instances <= 0)
	{
		instances = 0;
		channel = -1;
	}
}

void sfxSound::reset()
{
	Mix_FreeChunk(sfx);
	sfx = NULL;
	ready = false;

	if(channel > -1)
		g_PlayingSoundChannels[channel] = NULL;

	channel = -1;
}

int sfxSound::isplaying()
{
	if(channel == -1)
		return false;

	return Mix_Playing(channel);
}


sfxMusic::sfxMusic()
{
	paused = false;
	ready = false;
	music = NULL;
}

sfxMusic::~sfxMusic()
{}

bool sfxMusic::load(const string& filename)
{
	if(music)
		reset();

    cout << "load "<< filename<< "..."<< endl;
    // streaming music from a file seems to crash after awhile - libfat issue?
	//music = Mix_LoadMUS(filename.c_str());

    // read in entire file
    FILE *fp = fopen(filename.c_str(), "r");
    if (!fp) return false;
    struct stat fileinfo;
	fstat(fp->_file, &fileinfo);
	int size = fileinfo.st_size;
	musicBuffer = (Uint8 *)memalign(32, size);
	fread(musicBuffer, 1, size, fp);
	fclose(fp);

	// put into SDL RW structure
	musicRW = SDL_RWFromMem(musicBuffer, size);
	if (!musicRW) return false;

	// load into mixer
	music = Mix_LoadMUS_RW(musicRW);

	if(!music)
	{
	    printf("Error Loading Music: %s\n", Mix_GetError());
		return false;
	}

	Mix_HookMusicFinished(&musicfinished);

	ready = true;

	return true;
}

void sfxMusic::play(bool fPlayonce, bool fResume)
{
	if(!music)
		return;
	Mix_PlayMusic(music, (fPlayonce ? 0 : -1));
	fResumeMusic = fResume;
}

void sfxMusic::stop()
{
	if(!music)
		return;

	Mix_HaltMusic();
}

void sfxMusic::sfx_pause()
{
	if(!music)
		return;

	paused = !paused;

	if(paused)
		Mix_PauseMusic();
	else
		Mix_ResumeMusic();
}

void sfxMusic::reset()
{
	if(music)
		Mix_FreeMusic(music);
	music = NULL;
	SDL_FreeRW(musicRW);
	free(musicBuffer);
	ready = false;
}

int sfxMusic::isplaying()
{
	return Mix_PlayingMusic();
}
