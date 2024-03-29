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

#include "LBPlay.h"
#include "LBPlayRes.h"


/* Prototypes */
INT	main(int argc, char *argv[]);
BOOL	Init(VOID);
LONG	MessageBox(HWND, LONG, PSZ, LONG, BOOL);
VOID APIENTRY ExitProc(ULONG);
MRESULT EXPENTRY MainDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
MRESULT EXPENTRY KaraWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
MRESULT EXPENTRY InfoWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);


/* Constants */

#define RETURN_SUCCESS	      0		 /* successful return in DosExit    */
#define RETURN_ERROR	      1		 /* error return in DosExit	    */
#define BEEP_WARN_FREQ	     60		 /* frequency of warning beep	    */
#define BEEP_WARN_DUR	    100		 /* duration of warning beep	    */
#define MESSAGELEN	     80		 /* maximum length for messages	    */

#define  KARLEN         60      // Max. Karaoke line length
#define  SKIPWAIT       2       // Time to wait if skipping (xINDICPAUSE)
#define  SLIDERTICKS    128     // Ticks in the slider
