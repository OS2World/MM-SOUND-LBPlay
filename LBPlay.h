/* 

LBPlay - An universal MIDI player/front end by Lesha Bogdanow
Copyright (C) 1999  Lesha Bogdanow

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include "..\midilib.h"
#include "..\MIDIPlay.h"

/* Prototypes */

int KaraParse(char EOLChar);
void SeedRnd();
void Play_Stop();
int CmdParse(int argc, char *argv[]);
void ParmsCleanup();
void ShuffleLst(CHAR *pszFileName, int Files);
int LoadFile(CHAR *pszName);

#define CFGINFOLEN	1024		// Config info buffer size


/* Globals */

struct KDATA {
   ULONG time;
   ULONG ipt;
   };

extern CHAR    		*pszFileName;	// Ptr to the playlist
extern int     		Files;		// Files in playlist */
extern int		Cycle;
extern int		Shuffle;
extern char		Kara;		// Global Karaoke setting
extern char     	Karaoke;		// Current karaoke setting
extern struct KDATA	*KarData;
extern char     	*KarBuf;
extern int		KarEntries;
extern ULONG		TotalTime;
extern LONG	    	StartFrom;
extern int Pausing;
extern int KarP;
extern int		Playing;
extern int		PlayDone;
extern MIDIDATA		MIDIData;
extern EVENTHDR		*hp;
extern char		*Reset;
extern char		*PlayParms[];
extern int		CodePage;


/* Constants */
#define  FILENOTFOUND                  9001
#define  CANTREAD                      9002

#define K_AUTO 100			 // Karaoke - auto select

#define  CTLPAUSE	100	// pause (ms) for control/indication thread
#define  SKIPSTEP       1000    // Step for fforward/rewind

#define MINKARAENTRIES   3	// Minimum number of non-zero karaoke entries

#define MAXPLAYPARMS	256	// Maximum No of player parameters

#define MAX_CUST_RESET	16	// Maximum length of custom reset

#define CFGEXT ".cfg"		// Config file extension
