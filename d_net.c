//
// Copyright (C) 1993-1996 Id Software, Inc.
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
//  DOOM Network game communication and protocol,
//  all OS independend parts.
//

#include "m_menu.h"
#include "i_system.h"
#include "g_game.h"
#include "doomdef.h"
#include "doomstat.h"

doomcom_t *doomcom;
doomdata_t *netbuffer; // points inside doomcom

//
// NETWORKING
//
// gametic is the tic about to (or currently being) run
// maketic is the tick that hasn't had control made for it yet
// nettics[] has the maketics for all players
//
// a gametic cannot be run until nettics[] > gametic for all players
//
#define PL_DRONE 0x80 // bit flag in doomdata->player

ticcmd_t localcmds[BACKUPTICS];

int nettics;
int resendto;

int maketic;
int skiptics;
int ticdup;

void D_ProcessEvents(void);
void G_BuildTiccmd(ticcmd_t *cmd);
void D_DoAdvanceDemo(void);

//
//
//
int ExpandTics(int low)
{
	int delta;

	delta = low - (maketic & 0xff);

	if (delta >= -64 && delta <= 64)
		return (maketic & ~0xff) + low;
	if (delta > 64)
		return (maketic & ~0xff) - 256 + low;
	if (delta < -64)
		return (maketic & ~0xff) + 256 + low;

	return 0;
}

//
// GetPackets
//

void GetPackets(void)
{
	int realend;
	int realstart;

	// to save bytes, only the low byte of tic numbers are sent
	// Figure out what the rest of the bytes are
	realstart = ExpandTics(netbuffer->starttic);
	realend = (realstart + netbuffer->numtics);

	// check for out of order / duplicated packet
	if (realend == nettics)
		return;

	if (realend < nettics)
	{
		return;
	}

	// check for a missed packet
	if (realstart > nettics)
	{
		// stop processing until the other system resends the missed tics
		return;
	}
	
	nettics = realend;
}

//
// NetUpdate
// Builds ticcmds for console player,
// sends out a packet
//
int gametime;

void NetUpdate(void)
{
	int nowtime;
	int newtics;
	int i, j;
	int realstart;
	int gameticdiv;

	// check time
	nowtime = ticcount / ticdup;
	newtics = nowtime - gametime;
	gametime = nowtime;

	if (newtics <= 0) // nothing new to update
		return;

	if (skiptics <= newtics)
	{
		newtics -= skiptics;
		skiptics = 0;
	}
	else
	{
		skiptics -= newtics;
		newtics = 0;
	}

	// build new ticcmds for console player
	gameticdiv = gametic / ticdup;
	for (i = 0; i < newtics; i++)
	{
		I_StartTic();
		D_ProcessEvents();
		if (maketic - gameticdiv >= BACKUPTICS / 2 - 1)
			break; // can't hold any more

		G_BuildTiccmd(&localcmds[resendto % BACKUPTICS]);
		maketic++;
	}

	if (singletics)
		return; // singletic update is syncronous

	netbuffer->starttic = realstart = resendto;
	netbuffer->numtics = maketic - realstart;

	resendto = maketic;

	// listen for other packets
	GetPackets();
}

//
// D_CheckNetGame
// Works out player numbers among the net participants
//
void D_CheckNetGame(void)
{
	// which tic to start sending
	resendto = 0;
	nettics = 0;

	// I_InitNetwork sets doomcom and netgame
	I_InitNetwork();

	netbuffer = &doomcom->data;

	printf("startskill %i  deathmatch: %i  startmap: %i  startepisode: %i\n",
		   startskill, 0, startmap, startepisode);

	// read values out of doomcom
	ticdup = doomcom->ticdup;

	playeringame[0] = true;

	printf("player %i of %i (%i nodes)\n",
		   1, 1, 1);
}

//
// TryRunTics
//
int oldnettics;

extern boolean advancedemo;

void TryRunTics(void)
{
	int i;
	int lowtic;
	int entertic;
	static int oldentertics;
	int realtics;
	int availabletics;
	int counts;

	// get real tics
	entertic = ticcount / ticdup;
	realtics = entertic - oldentertics;
	oldentertics = entertic;

	// get available tics
	NetUpdate();

	lowtic = MAXINT;
	if (nettics < lowtic)
		lowtic = nettics;
	availabletics = lowtic - gametic / ticdup;

	// decide how many tics to run
	if (realtics < availabletics - 1)
		counts = realtics + 1;
	else if (realtics < availabletics)
		counts = realtics;
	else
		counts = availabletics;

	if (counts < 1)
		counts = 1;

	// wait for new tics if needed
	while (lowtic < gametic / ticdup + counts)
	{
		NetUpdate();
		lowtic = MAXINT;

		if (nettics < lowtic)
			lowtic = nettics;

		// don't stay in here forever -- give the menu a chance to work
		if (ticcount / ticdup - entertic >= 20)
		{
			M_Ticker();
			return;
		}
	}

	// run the count * ticdup dics
	while (counts--)
	{
		for (i = 0; i < ticdup; i++)
		{
			if (advancedemo)
				D_DoAdvanceDemo();
			M_Ticker();
			G_Ticker();
			gametic++;
		}
		NetUpdate(); // check for new console commands
	}
}
