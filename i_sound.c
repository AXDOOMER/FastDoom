//
// Copyright (C) 1993-1996 Id Software, Inc.
// Copyright (C) 1993-2008 Raven Software
// Copyright (C) 2016-2017 Alexey Khokholov (Nuke.YKT)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//  System interface for sound.
//

#include <stdio.h>

#include "dmx.h"

#include "i_ibm.h"
#include "i_system.h"
#include "s_sound.h"
#include "i_sound.h"
#include "sounds.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "doomdef.h"
#include "doomstat.h"

//
// I_StartupTimer
//

int tsm_ID = -1;

void I_StartupTimer(void)
{
    extern int I_TimerISR(void);

    printf("I_StartupTimer()\n");
    // installs master timer.  Must be done before StartupTimer()!
    tsm_ID = TSM_NewService(I_TimerISR, 35, 0, 0);
    if (tsm_ID == -1)
    {
        I_Error("Can't register 35 Hz timer w/ DMX library");
    }
}

void I_ShutdownTimer(void)
{
    TSM_DelService(tsm_ID);
    TSM_Remove();
}

//
// Sound header & data
//
const char snd_prefixen[] = {'P', 'P', 'A', 'S', 'S', 'S', 'M', 'M', 'M', 'S', 'S', 'S'};

int dmxCodes[NUM_SCARDS]; // the dmx code for a given card

int snd_SBport, snd_SBirq, snd_SBdma; // sound blaster variables
int snd_Mport;                        // midi variables

int snd_MusicVolume; // maximum volume for music
int snd_SfxVolume;   // maximum volume for sound

int snd_SfxDevice;   // current sfx card # (index to dmxCodes)
int snd_MusicDevice; // current music card # (index to dmxCodes)
int snd_DesiredSfxDevice;
int snd_DesiredMusicDevice;

void I_PauseSong(int handle)
{
    MUS_PauseSong(handle);
}

void I_ResumeSong(int handle)
{
    MUS_ResumeSong(handle);
}

void I_SetMusicVolume(int volume)
{
    MUS_SetMasterVolume(volume);
    snd_MusicVolume = volume;
}

//
// Song API
//

int I_RegisterSong(void *data)
{
    int rc = MUS_RegisterSong(data);
    return rc;
}

//
// Stops a song.  MUST be called before I_UnregisterSong().
//
void I_StopSong(int handle)
{
    int rc;
    rc = MUS_StopSong(handle);

    // Fucking kluge pause
    {
        int s;
        for (s = ticcount; ticcount - s < 10;)
            ;
    }
}

void I_PlaySong(int handle, boolean looping)
{
    int rc;
    rc = MUS_ChainSong(handle, looping ? handle : -1);
    rc = MUS_PlaySong(handle, snd_MusicVolume);
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    sprintf(namebuf, "D%c%s", snd_prefixen[snd_SfxDevice], sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, void *data, int vol, int sep)
{
    return SFX_PlayPatch(data, sep, vol);
}

void I_StopSound(int handle)
{
    SFX_StopPatch(handle);
}

int I_SoundIsPlaying(int handle)
{
    return SFX_Playing(handle);
}

void I_UpdateSoundParams(int handle, int vol, int sep)
{
    SFX_SetOrigin(handle, sep, vol);
}

//
// Sound startup stuff
//

void I_sndArbitrateCards(void)
{
    boolean gus, adlib, sb, midi, codec, ensoniq;
    int i, wait, dmxlump;

    snd_SfxVolume = 127;
    snd_SfxDevice = snd_DesiredSfxDevice;
    snd_MusicDevice = snd_DesiredMusicDevice;

    //
    // check command-line parameters- overrides config file
    //
    if (M_CheckParm("-nosound"))
    {
        snd_MusicDevice = snd_SfxDevice = snd_none;
    }
    if (M_CheckParm("-nosfx"))
    {
        snd_SfxDevice = snd_none;
    }
    if (M_CheckParm("-nomusic"))
    {
        snd_MusicDevice = snd_none;
    }

    if (snd_MusicDevice > snd_MPU && snd_MusicDevice <= snd_MPU3)
    {
        snd_MusicDevice = snd_MPU;
    }
    if (snd_MusicDevice == snd_SB)
    {
        snd_MusicDevice = snd_Adlib;
    }
    if (snd_MusicDevice == snd_PAS)
    {
        snd_MusicDevice = snd_Adlib;
    }

    //
    // figure out what i've got to initialize
    //
    gus = snd_MusicDevice == snd_GUS || snd_SfxDevice == snd_GUS;
    sb = snd_SfxDevice == snd_SB || snd_MusicDevice == snd_SB;
    ensoniq = snd_SfxDevice == snd_ENSONIQ;
    codec = snd_SfxDevice == snd_CODEC;
    adlib = snd_MusicDevice == snd_Adlib;
    midi = snd_MusicDevice == snd_MPU;

    //
    // initialize whatever i've got
    //
    if (ensoniq)
    {
        if (ENS_Detect())
        {
            printf("Dude.  The ENSONIQ ain't responding.\n");
        }
    }
    if (codec)
    {
        if (CODEC_Detect(&snd_SBport, &snd_SBdma))
        {
            printf("CODEC.  The CODEC ain't responding.\n");
        }
    }
    if (gus)
    {
        fprintf(stderr, "GUS1\n");
        if (GF1_Detect())
        {
            printf("Dude.  The GUS ain't responding.\n");
        }
        else
        {
            fprintf(stderr, "GUS2\n");
            if (commercial)
            {
                dmxlump = W_GetNumForName("DMXGUSC");
            }
            else
            {
                dmxlump = W_GetNumForName("DMXGUS");
            }
            GF1_SetMap(W_CacheLumpNum(dmxlump, PU_CACHE), lumpinfo[dmxlump].size);
        }
    }
    if (sb)
    {
        if (SB_Detect(&snd_SBport, &snd_SBirq, &snd_SBdma, 0))
        {
            printf("SB isn't responding at p=0x%x, i=%d, d=%d\n",
                   snd_SBport, snd_SBirq, snd_SBdma);
        }
        else
        {
            SB_SetCard(snd_SBport, snd_SBirq, snd_SBdma);
        }
    }

    if (adlib)
    {
        if (AL_Detect(&wait, 0))
        {
            printf("Dude.  The Adlib isn't responding.\n");
        }
        else
        {
            AL_SetCard(wait, W_CacheLumpName("GENMIDI", PU_STATIC));
        }
    }

    if (midi)
    {
        if (MPU_Detect(&snd_Mport, &i))
        {
            printf("The MPU-401 isn't reponding @ p=0x%x.\n", snd_Mport);
        }
        else
        {
            MPU_SetCard(snd_Mport);
        }
    }
}

//
// I_StartupSound
// Inits all sound stuff
//
void I_StartupSound(void)
{
    int rc;

    //
    // initialize dmxCodes[]
    //
    dmxCodes[0] = 0;
    dmxCodes[snd_PC] = AHW_PC_SPEAKER;
    dmxCodes[snd_Adlib] = AHW_ADLIB;
    dmxCodes[snd_SB] = AHW_SOUND_BLASTER;
    dmxCodes[snd_PAS] = AHW_MEDIA_VISION;
    dmxCodes[snd_GUS] = AHW_ULTRA_SOUND;
    dmxCodes[snd_MPU] = AHW_MPU_401;
    dmxCodes[snd_AWE] = AHW_AWE32;
    dmxCodes[snd_ENSONIQ] = AHW_ENSONIQ;
    dmxCodes[snd_CODEC] = AHW_CODEC;

    //
    // inits sound library timer stuff
    //
    I_StartupTimer();

    //
    // pick the sound cards i'm going to use
    //
    I_sndArbitrateCards();

    //
    // inits DMX sound library
    //
    printf("  calling DMX_Init\n");

    rc = DMX_Init(SND_TICRATE, SND_MAXSONGS, dmxCodes[snd_MusicDevice],
                  dmxCodes[snd_SfxDevice]);
}
//
// I_ShutdownSound
// Shuts down all sound stuff
//
void I_ShutdownSound(void)
{
    int s;
    S_PauseSound();
    s = ticcount + 30;
    while (s != ticcount) {}
    DMX_DeInit();
}

void I_SetChannels(int channels)
{
    int samplerate;

    samplerate = lowSound ? 8000 : 11025;

    WAV_PlayMode(channels, samplerate);
}
