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

#define USE_OS2_TOOLKIT_HEADERS
#define INCL_WIN
#define INCL_HELP
#define INCL_WINHEAP
#define INCL_WINMENU		   /* For menu control function */
#define INCL_WINDIALOGS
#define INCL_WINMESSAGEMGR
#define INCL_DOSPROCESS			   /* For init function */
#define INCL_GPIPRIMITIVES
#define INCL_GPIBITMAPS
#define INCL_BASE
#define INCL_ERRORS
#define INCL_DOSMODULEMGR
#define INCL_DOSSEMAPHORES
#define INCL_DOSFILEMGR
#define INCL_DOSQUEUES
#define INCL_DOSPROCESS
#define INCL_DOSMEMMGR
#define INCL_DOSDATETIME


#include <stddef.h>
#include <os2.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <meerror.h>
#include <mididll.h>
#include <midios2.h>


#include "LBPlayPM.h"

/* My timer */
#define MY_TIMER	1
/* Timer time (ms) */
#define TIMER_DELAY	100
// Skipping pause (in TIMER_DELAYs)
#define SKIP_PAUSE	5
#define PAUSE_FOREVER	1000	// A value t indicate "pause until unpaused"

// Global Variables

HAB   hab;			    /* anchor block for the process */
HMQ   hmq;		    /* handle to the process' message queue */
HWND  hwndMain;			/* handle to the main client window */
HWND  hwndKaraoke;
HWND  hwndKaraFrame=0;;
HWND  hwndKaraMLE;
HWND  hwndInfo;
HWND  hwndInfoFrame=0;;
HWND  hwndInfoMLE;
HSWITCH hswitch;

int		Close=FALSE;
TID		LoadTID=0;
int		FileNo;			/* File being played */
int		LoadErr=FALSE;		/* Loading failed */
int		LoadOk=FALSE;		// Loading succeeeded

int		SlMoving=FALSE;		// TRUE while the program updates slider

CHAR  szTitle[MESSAGELEN];
CHAR  szLoading[MESSAGELEN];
CHAR  szCleanup[MESSAGELEN];
CHAR  szKaraoke[MESSAGELEN];
CHAR  szInfo[MESSAGELEN];

static char *CfgInfo;

static HACCEL hAccel;

static char szIniApp[]="LBPlay";
static char szKeyMainWindowPos[]="MainWindowPos";
static char szKeyKaraWindowPos[]="KaraWindowPos";
static char szKeyInfoWindowPos[]="InfoWindowPos";

static int RewindPressed=FALSE;
static int FFwdPressed=FALSE;

#define INFOALLOC 8192			//Memory to be allocated for info
static char *InfoBuf=NULL;

// 
//      #define BDS_HILITED                0x0100
//      #define BDS_DISABLED               0x0200
//      #define BDS_DEFAULT                0x0400
#define SHIFT3DX	1
#define SHIFT3DY	-1


void DrawButton(USERBUTTON *ub,int BmpID) {
   HBITMAP hbm;
   BITMAPINFOHEADER bmpData;
   RECTL rectDst;
   int sx,sy;

   if (ub->fsState&BDS_DISABLED) {
      hbm=GpiLoadBitmap(ub->hps, NULLHANDLE, IDB_DISABLED, 0L, 0L);
      sx=0;
      sy=0;
      }
   else if (ub->fsState&BDS_HILITED) {
      hbm=GpiLoadBitmap(ub->hps, NULLHANDLE, IDB_HILITED, 0L, 0L);
      sx=SHIFT3DX;
      sy=SHIFT3DY;
      }
   else {
      hbm=GpiLoadBitmap(ub->hps, NULLHANDLE, IDB_NORMAL, 0L, 0L);
      sx=-SHIFT3DX;
      sy=-SHIFT3DY;
      }
   WinQueryWindowRect(ub->hwnd,&rectDst);
   WinDrawBitmap(ub->hps,hbm,NULL,(POINTL *)&rectDst,0,0,DBM_STRETCH);
   GpiDeleteBitmap(hbm);
   hbm=GpiLoadBitmap(ub->hps, NULLHANDLE, BmpID, 0L, 0L);
   GpiQueryBitmapParameters(hbm,&bmpData);
   sy+=(rectDst.yTop-rectDst.yBottom-bmpData.cy)/2;
   if (sy<0) sy=0;
   sx+=(rectDst.xRight-rectDst.xLeft-bmpData.cx)/2;
   if (sx<0) sx=0;
   rectDst.xLeft+=sx;
   rectDst.xRight=rectDst.xLeft+bmpData.cx;
   rectDst.yBottom+=sy;
   rectDst.yTop=rectDst.yBottom+bmpData.cy;
   WinDrawBitmap(ub->hps,hbm,NULL,(POINTL *)&rectDst,0,0,DBM_STRETCH);
   GpiDeleteBitmap(hbm);
   return;   
   }


void SetPause(BOOL State) {
   static StateSet=TRUE;

   if (State!=StateSet) {
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_PAUSE),FALSE);
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_PAUSE),TRUE);
      StateSet=State;
      }
   return;
   }

void SetKara(BOOL State) {
   static StateSet=TRUE;

   if (State!=StateSet) {
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_KARA),FALSE);
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_KARA),TRUE);
      StateSet=State;
      }
   return;
   }

void SetInfo(BOOL State) {
   static StateSet=TRUE;

   if (State!=StateSet) {
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_INFO),FALSE);
      WinShowWindow(WinWindowFromID(hwndMain,ID_MA_INFO),TRUE);
      StateSet=State;
      }
   return;
   }

#define RATETIME 100	//Time interval for data rate measurement
void MakeInfo() {
   int PortsUsed[256];
   EVENTHDR *eh;
   int i,Polyphony=0,MaxPolyphony=0,cports=0;
   struct t_ManufInfo {
      char id;
      char name[32];
      int used;
      } ManufInfo[]={{1,"Sequential Circuits"},{2,"Big Briar"},
      {3,"Octave/Plateau"},{4,"Moog"},{5,"Passport Designs"},{6,"Lexicon"},
      {7,"Kurzweil"},{8,"Fender"},{9,"Gulbransen"},{0xA,"Delta Labs"},
      {0xB,"Sound Comp"},{0xC,"General Electro"},{0xD,"Techmar"},
      {0xE,"Matthews Research"},{0x10,"Oberheim"},{0x11,"PAIA"},
      {0x12,"Simmons"},{0x13,"Gentle Electric"},{0x14,"Fairlight"},
      {0x1B,"Peavey"},{0x15,"JL Cooper"},{0x16,"Lowery"},{0x17,"Lin"},
      {0x18,"Emu"},{0x20,"Bon Tempi"},{0x21,"S.I.E.L"},{0x23,"SyntheAxe"},
      {0x24,"Hohner"},{0x25,"Crumar"},{0x26,"Solton"},{0x27,"Jellinghaus MS"},
      {0x28,"CTS"},{0x29,"PPG"},{0x2F,"Elka"},{0x40,"Kawai"},{0x41,"Roland"},
      {0x42,"Korg"},{0x43,"Yamaha"},{0x44,"Casio"},{0x45,"Akai"},
      {0x7D,"Educational use"},{0x7E,"General MIDI"},{0,"Unknown"}
      };

   if (InfoBuf) return;
   InfoBuf=malloc(INFOALLOC);
   *InfoBuf=0;
   if (CfgInfo) sprintf(InfoBuf,"Player info: %s",CfgInfo);
   if (MIDIData.warns) {
      sprintf(InfoBuf+strlen(InfoBuf),"File loading warnings:\n");
      if (MIDIData.warns&MW_BADTEMPO)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad tempo metaevent.\n");
      if (MIDIData.warns&MW_BADMTHD)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad MThd chunk.\n");
      if (MIDIData.warns&MW_EXTRAMTHD)
         sprintf(InfoBuf+strlen(InfoBuf),"   Extra MThd chunk.\n");
      if (MIDIData.warns&MW_BADFMT0)
         sprintf(InfoBuf+strlen(InfoBuf),"   More than 1 track in fmt 0.\n");
      if (MIDIData.warns&MW_BADSTATUS)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad message status.\n");
      if (MIDIData.warns&MW_BADRSTATUS)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad running status.\n");
      if (MIDIData.warns&MW_BADMTRK)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad MTrk chunk.\n");
      if (MIDIData.warns&MW_BADCHUNK)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad chunk.\n");
      if (MIDIData.warns&MW_NOMTHD)
         sprintf(InfoBuf+strlen(InfoBuf),"   No MThd chunk.\n");
      if (MIDIData.warns&MW_BADTRKCNT)
         sprintf(InfoBuf+strlen(InfoBuf),"   Track number mismatch.\n");
      if (MIDIData.warns&MW_BADTIMESIGN)
         sprintf(InfoBuf+strlen(InfoBuf),"   Bad time signature metaevent.\n");
      }


   sprintf(InfoBuf+strlen(InfoBuf),"File format: type%u\n",MIDIData.fmt);
   if (MIDIData.fmt) sprintf(InfoBuf+strlen(InfoBuf),"Tracks: %u\n",MIDIData.trks);
   sprintf(InfoBuf+strlen(InfoBuf),"Events: %u\n",MIDIData.events);
   sprintf(InfoBuf+strlen(InfoBuf),"Duration: %u:%02u\n",(TotalTime/1000)/60,(TotalTime/1000)%60);

   for (i=0;i<256;i++) PortsUsed[i]=FALSE;
   for (i=0;ManufInfo[i].id;i++) ManufInfo[i].used=FALSE;
   ManufInfo[i].used=FALSE;
   eh=MIDIData.pfirst;
   while (eh) {
      if ((eh->status==0xFF)&&(eh->type==0x21)&&(eh->len>0))
         PortsUsed[eh->data[0]]=TRUE;
      else if (eh->status==0xF0) {
         for (i=0;ManufInfo[i].id&&(ManufInfo[i].id!=eh->data[0]);i++);
         ManufInfo[i].used=TRUE;
         }
      else if ((eh->status<0xA0)&&(eh->linked)) {
         if (eh->index<((EVENTHDR *)eh->linked)->index) Polyphony++;
         else Polyphony--;
         if (Polyphony>MaxPolyphony) MaxPolyphony=Polyphony;
         }
      eh=eh->pnext;
      }

   for (i=0;i<256;i++) if (PortsUsed[i]) cports++;
   
   if (!cports) cports++;
   sprintf(InfoBuf+strlen(InfoBuf),"Ports: %u\n",cports);
   sprintf(InfoBuf+strlen(InfoBuf),"Max. polyphony: %u notes\n",MaxPolyphony);
   for (i=0;ManufInfo[i].id&&(!ManufInfo[i].used);i++);
   if (ManufInfo[i].used) {
      sprintf(InfoBuf+strlen(InfoBuf),"Target synths:\n");
      for (i=0;ManufInfo[i].id;i++) if (ManufInfo[i].used) 
         sprintf(InfoBuf+strlen(InfoBuf),"   %s\n",ManufInfo[i].name);
      if (ManufInfo[i].used) sprintf(InfoBuf+strlen(InfoBuf),"   %s\n",ManufInfo[i].name);
      }

   InfoBuf=realloc(InfoBuf,strlen(InfoBuf)+1);
   return;
   }

void InfoFree() {
   if (InfoBuf) free(InfoBuf);
   InfoBuf=NULL;
   }

// KarBuf codepage translation
void KaraXlat() {
   int ccp;
   char c,*p;

   if ((!CodePage)||(!KarBuf)) return;
   ccp=WinQueryCp(hmq);
   if (!ccp) return;
   
   for(p=KarBuf;*p;p++) if (*p>0x20) {
      c=WinCpTranslateChar(hab,CodePage,*p,ccp);
      if (c) *p=c;
      }
   }


VOID APIENTRY LoadThread(ULONG ulDummy) {
   int    rc;

   rc=LoadFile(pszFileName+FileNo*CCHMAXPATH);
   if (rc) {
      LoadErr=TRUE;
      LoadTID=0;
      WinPostMsg(hwndMain,WM_USER,NULL, NULL);	/* Start everything */
      return;
      }

   LoadOk=TRUE;
   LoadTID=0;
   WinPostMsg(hwndMain,WM_USER,NULL, NULL);	/* Start everything */
   return;
   }



void SetFileList(CHAR *pszFileName, int Files) {
   int i;
   HWND hwndList;

   hwndList=WinWindowFromID(hwndMain,ID_MA_LIST);
   WinSendMsg(hwndList,LM_DELETEALL,(MPARAM)NULL,(MPARAM)NULL ) ;
   for (i=0;i<Files;i++)    
      WinSendMsg(hwndList,LM_INSERTITEM,
                 MPFROMSHORT(LIT_END),MPFROMP((PSZ)pszFileName+i*CCHMAXPATH));

   if (Files>1) WinEnableWindow(hwndList,TRUE);
   return;
   }

void Terminate() {

   Play_Stop();   
   PlayCleanup();
   ParmsCleanup();
   WinStoreWindowPos(szIniApp,szKeyMainWindowPos,hwndMain);
   WinPostMsg( hwndMain, WM_QUIT, (MPARAM)0,(MPARAM)0 );
   return;
   }


/****************************************************************\
 *  Main routine						*
 *--------------------------------------------------------------*
 *								*
 *  Name:    main(int argc, char *argv[])			*
 *								*
 *  Purpose: Initializes the PM environment, calls the		*
 *	     initialization routine, creates the main window,	*
 *	     and polls the message queue.			*
 *								*
 *  Usage:							*
 *								*
 *  Method:  -obtains anchor block handle and creates message	*
 *		queue						*
 *	     -calls the initialization routine			*
 *	     -creates the main frame window which creates the	*
 *		main client window				*
 *	     -polls the message queue via Get/Dispatch Msg loop *
 *	     -upon exiting the loop, exits			*
 *								*
 *  Returns: 1 - if sucessful execution completed		*
 *	     0 - if error					*
 *								*
\****************************************************************/
INT main(int argc, char *argv[])
{
   QMSG qmsg;					      /* message structure */

   hab = WinInitialize(0);
   if(!hab)  {
       DosBeep(BEEP_WARN_FREQ, BEEP_WARN_DUR);
       return(RETURN_ERROR);
   }
   hmq = WinCreateMsgQueue(hab, 0);
   if(!hmq)  {
       DosBeep(BEEP_WARN_FREQ, BEEP_WARN_DUR);
       WinTerminate(hab);
       return(RETURN_ERROR);
   }
   if(!Init()) {
       MessageBox(HWND_DESKTOP, IDMSG_INITFAILED, "Error !",
			   MB_OK | MB_ERROR | MB_MOVEABLE, TRUE);
       DosExit(EXIT_PROCESS, RETURN_ERROR);
       }
   if (CmdParse(argc,argv)) {
       MessageBox(HWND_DESKTOP, IDMSG_SYNTAXERROR, "Error !",
			   MB_OK | MB_ERROR | MB_MOVEABLE, TRUE);
       DosExit(EXIT_PROCESS, RETURN_ERROR);
       }
   hAccel=WinLoadAccelTable(hab,(HMODULE)NULL, ID_ACCEL);
   WinSetAccelTable(hab, hAccel, hwndMain);

   CfgInfo=malloc(CFGINFOLEN);
   if (PlayInit(PlayParms,Reset,CfgInfo,CFGINFOLEN)) {
      MessageBox(HWND_DESKTOP, IDMSG_RTMIDIFAILED, "Error !",
		   MB_OK | MB_ERROR | MB_MOVEABLE, TRUE);
      DosExit(EXIT_PROCESS, RETURN_ERROR);
      }

/*   InitHelp(); */

   SetKara(Karaoke);
   SeedRnd();

   if(!WinStartTimer(hab,hwndMain,MY_TIMER,TIMER_DELAY)) {
       MessageBox(HWND_DESKTOP, IDMSG_INITFAILED, "Error !",
			   MB_OK | MB_ERROR | MB_MOVEABLE, TRUE);
       DosExit(EXIT_PROCESS, RETURN_ERROR);
       }

   if (Shuffle) ShuffleLst(pszFileName,Files);
   SetFileList(pszFileName,Files);
   FileNo=0;

   
   WinPostMsg(hwndMain,WM_USER,NULL, NULL);	/* Start everything */

   while(WinGetMsg(hmq, (PQMSG)&qmsg, 0L, 0L, 0L))
	   WinDispatchMsg(hmq, (PQMSG)&qmsg);
					   /* destroy the help instance */
/*   DestroyHelpInstance(); */
				    /* will normally be put in ExitProc */
   free(CfgInfo);
   WinStopTimer(hab,hwndMain,MY_TIMER);
   DosExit(EXIT_PROCESS, RETURN_SUCCESS);
   return 0;
}							       /* main() */

BOOL Init(VOID)
{
    SWCNTRL SwData;
    PID pid;

    /* Add ExitProc to the exit list to handle the exit processing */
    if(DosExitList(EXLST_ADD, ExitProc)) return FALSE;

   if (!WinLoadString(hab, 0, IDS_TITLE, MESSAGELEN, szTitle)) return FALSE;
   if (!WinLoadString(hab, 0, IDS_LOAD, MESSAGELEN, szLoading)) return FALSE;
   if (!WinLoadString(hab, 0, IDS_CLEANUP, MESSAGELEN, szCleanup)) return FALSE;
   if (!WinLoadString(hab, 0, IDS_KARAOKE, MESSAGELEN, szKaraoke)) return FALSE;
   if (!WinLoadString(hab, 0, IDS_INFO, MESSAGELEN, szInfo)) return FALSE;



 /* register window classes */
     if (!WinRegisterClass(hab,(PSZ)"KARAOKE",(PFNWP)KaraWndProc,
			CS_SIZEREDRAW | CS_SYNCPAINT | CS_CLIPCHILDREN,0))
         return FALSE;
     if (!WinRegisterClass(hab,(PSZ)"INFO",(PFNWP)InfoWndProc,
			CS_SIZEREDRAW | CS_SYNCPAINT | CS_CLIPCHILDREN,0))
         return FALSE;


/* Create subwindows */
    hwndMain=WinLoadDlg(HWND_DESKTOP, HWND_DESKTOP, (PFNWP)MainDlgProc,
			(HMODULE) NULL, IDD_MAIN, NULL);
    WinQueryWindowProcess(hwndMain,&pid,NULL);
    SwData.hwnd=hwndMain;
    SwData.hwndIcon=NULLHANDLE;
    SwData.hprog=NULLHANDLE;
    SwData.idProcess=pid;
    SwData.idSession=0;
    SwData.uchVisibility=SWL_VISIBLE;
    SwData.fbJump=SWL_JUMPABLE;
    WinLoadString(hab, 0, IDS_SWTITLE, MESSAGELEN, SwData.szSwtitle);
    hswitch=WinCreateSwitchEntry(hab,&SwData);
    return TRUE;
}							  /* Init() */

// Create Karaoke window 
int CreateKaraWindow() {
   ULONG ctlKara= FCF_TITLEBAR|FCF_SIZEBORDER|FCF_MINMAX|FCF_SHELLPOSITION|
                  FCF_SYSMENU|FCF_ICON|FCF_TASKLIST;
   RECTL  rcl;
   IPT iptOffset;

   if (!hwndKaraFrame) {
      hwndKaraFrame = WinCreateStdWindow(HWND_DESKTOP,/*WS_VISIBLE*/0,(PVOID)&ctlKara,
				      (PSZ)"KARAOKE",(PSZ)szKaraoke,
				      WS_VISIBLE,(HMODULE)NULL,ID_KARAOKE,
				      (PHWND)&hwndKaraoke);
       if (!hwndKaraFrame) return FALSE;
       WinRestoreWindowPos(szIniApp,szKeyKaraWindowPos,hwndKaraFrame);
       WinShowWindow(hwndKaraFrame,TRUE);
       WinSetAccelTable(hab, hAccel, hwndKaraFrame);
       if (!WinQueryWindowRect(hwndKaraoke, (PRECTL)&rcl)) return FALSE;
       hwndKaraMLE = WinCreateWindow(hwndKaraoke, WC_MLE, (PSZ)NULL,
                              MLS_HSCROLL|MLS_READONLY|MLS_VSCROLL|WS_VISIBLE,
                              rcl.xLeft,  rcl.yBottom,
                              rcl.xRight, rcl.yTop,
                              hwndKaraoke, HWND_TOP, ID_KARAMLE, NULL, NULL);
       if (!hwndKaraMLE) return FALSE;
       if (!WinFocusChange(HWND_DESKTOP,hwndKaraMLE,0)) return FALSE;
      }

      WinSendMsg(hwndKaraMLE, MLM_DISABLEREFRESH, NULL, NULL);
      WinSendMsg(hwndKaraMLE, MLM_SETSEL, MPFROMSHORT(NULL),
                 (MPARAM) WinSendMsg(hwndKaraMLE, MLM_QUERYTEXTLENGTH,NULL, NULL));
      WinSendMsg(hwndKaraMLE, MLM_CLEAR, NULL, NULL);
      WinSendMsg(hwndKaraMLE, MLM_SETIMPORTEXPORT,
                       MPFROMP((PBYTE)KarBuf),(MPARAM)strlen(KarBuf));
      iptOffset=0;
      WinSendMsg(hwndKaraMLE, MLM_IMPORT, MPFROMP(&iptOffset),
                          (MPARAM)strlen(KarBuf));
      WinSendMsg(hwndKaraMLE, MLM_ENABLEREFRESH, NULL, NULL);

    return TRUE;
    }

// Destroy Karaoke window
void DestroyKaraWindow() {
   if (!hwndKaraFrame) return;
   WinStoreWindowPos(szIniApp,szKeyKaraWindowPos,hwndKaraFrame);
   WinDestroyWindow(hwndKaraFrame);
   hwndKaraFrame=0;
   }

// Create Info window 
int CreateInfoWindow() {
   ULONG ctlInfo= FCF_TITLEBAR|FCF_SIZEBORDER|FCF_MINMAX|FCF_SHELLPOSITION|
                  FCF_SYSMENU|FCF_ICON|FCF_TASKLIST;
   RECTL  rcl;
   IPT iptOffset;

   if (!hwndInfoFrame) {
      hwndInfoFrame = WinCreateStdWindow(HWND_DESKTOP,0,(PVOID)&ctlInfo,
				      (PSZ)"INFO",(PSZ)szInfo,
				      WS_VISIBLE,(HMODULE)NULL,ID_INFO,
				      (PHWND)&hwndInfo);
      if (!hwndInfoFrame) return FALSE;
      WinRestoreWindowPos(szIniApp,szKeyInfoWindowPos,hwndInfoFrame);
      WinShowWindow(hwndInfoFrame,TRUE);
      WinSetAccelTable(hab, hAccel, hwndInfoFrame);
      if (!WinQueryWindowRect(hwndInfo, (PRECTL)&rcl)) return FALSE;
      hwndInfoMLE = WinCreateWindow(hwndInfo, WC_MLE, (PSZ)NULL,
                              MLS_HSCROLL|MLS_READONLY|MLS_VSCROLL|WS_VISIBLE,
                              rcl.xLeft,  rcl.yBottom,
                              rcl.xRight, rcl.yTop,
                              hwndInfo, HWND_TOP, ID_INFOMLE, NULL, NULL);
      if (!hwndInfoMLE) return FALSE;
      }

      WinSendMsg(hwndInfoMLE, MLM_DISABLEREFRESH, NULL, NULL);
      WinSendMsg(hwndInfoMLE, MLM_SETSEL, MPFROMSHORT(NULL),
                 (MPARAM) WinSendMsg(hwndInfoMLE, MLM_QUERYTEXTLENGTH,NULL, NULL));
      WinSendMsg(hwndInfoMLE, MLM_CLEAR, NULL, NULL);
      WinSendMsg(hwndInfoMLE, MLM_SETIMPORTEXPORT,
                       MPFROMP((PBYTE)InfoBuf),(MPARAM)strlen(InfoBuf));
      iptOffset=0;
      WinSendMsg(hwndInfoMLE, MLM_IMPORT, MPFROMP(&iptOffset),
                          (MPARAM)strlen(InfoBuf));
      WinSendMsg(hwndInfoMLE, MLM_ENABLEREFRESH, NULL, NULL);

    return TRUE;
    }

// Destroy Info window
void DestroyInfoWindow() {
   if (!hwndInfoFrame) return;
   WinStoreWindowPos(szIniApp,szKeyInfoWindowPos,hwndInfoFrame);
   WinDestroyWindow(hwndInfoFrame);
   hwndInfoFrame=0;
   }


VOID APIENTRY ExitProc(ULONG usTermCode)
				  /* code for the reason for termination */
{
   DestroyKaraWindow();
   DestroyInfoWindow();
   WinDestroyWindow(hwndMain);



   WinDestroyMsgQueue(hmq);

   WinTerminate(hab);

   DosExitList(EXLST_EXIT, (PFNEXITLIST)0L);	/* termination complete */

   return;
}							   /* ExitProc() */

LONG MessageBox(HWND hwndOwner, LONG IdMsg, PSZ pszMsg, LONG fsStyle,
		     BOOL bBeep)
{
    CHAR szText[MESSAGELEN];
    LONG usRet;

    if(!WinLoadMessage(hab, (HMODULE)NULL, IdMsg, MESSAGELEN, (PSZ)szText)) {
	WinAlarm(HWND_DESKTOP, WA_ERROR);
	return RETURN_ERROR;
        }
    if(bBeep) WinAlarm(HWND_DESKTOP, WA_ERROR);

    usRet = WinMessageBox(HWND_DESKTOP,
			 hwndOwner,
			 szText,
			 (PSZ)pszMsg,
			 IDM_MSGBOX,
			 fsStyle);
    return usRet;
}						    /* MessageBox() */

/* Proceed with next file or terminate if no files left to play */
void NextFile() {
   FileNo++;
   if (Cycle) {
      if (FileNo<0) FileNo=Files-1;
      if (FileNo==Files) {
         FileNo=0;
         if (Shuffle) {
            ShuffleLst(pszFileName,Files);
            SetFileList(pszFileName,Files);
            }
         }
      }
    if ((FileNo>=Files)||(FileNo<0)||(Close)) Terminate();
    return;
    }

void DisplayTime(unsigned long time) {
   char szTmp[16];
   sprintf(szTmp,"%u:%02u",(time/1000)/60,(time/1000)%60);
   WinSetWindowText(WinWindowFromID(hwndMain,ID_MA_CURTIME),szTmp);
   return;
   }

void SetSlider(unsigned long time) {
   SlMoving=TRUE;
   WinSendMsg(WinWindowFromID(hwndMain,ID_MA_SLIDER),SLM_SETSLIDERINFO,
              MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE),
              (MPARAM)(time*SLIDERTICKS/TotalTime));
   SlMoving=FALSE;
   return;
   }

void Rewind() {
   long CurTime;
   if (Playing||Pausing) {
      CurTime=GetCurTime();
      StartFrom=CurTime-SKIPSTEP;
      if (StartFrom<0) StartFrom=0;
      DisplayTime(StartFrom);
      SetSlider(StartFrom);
      if (Pausing<PAUSE_FOREVER) Pausing=SKIP_PAUSE;
      SetPause(TRUE);
      Play_Stop();
      }
   return;
   }

void FFwd() {
   long CurTime;
   if (Playing||Pausing) {
      CurTime=GetCurTime();
      StartFrom=CurTime+SKIPSTEP;
      if (StartFrom>TotalTime) StartFrom=TotalTime-1000;
      if (StartFrom<0) StartFrom=0;
      DisplayTime(StartFrom);
      SetSlider(StartFrom);
      if (Pausing<PAUSE_FOREVER) Pausing=SKIP_PAUSE;
      SetPause(TRUE);
      Play_Stop();
      }
   }

/* We're using my vars to figure out what's going on */
void MyEvent(HWND hwnd) {
   CHAR  szTmp[MESSAGELEN];
   EVENTHDR *kp;
   static ULONG MTime;
   int KarPP;


   PlaySet();
   if (PlayDone) {	/* Playing done */
      strcpy(szTmp,szTitle);
      strcat(szTmp,szCleanup);
      WinSetWindowText(hwndMain,szTmp);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_SLIDER),FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PAUSE),FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_REWND),FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_FFWD),FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PREV),FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_NEXT),FALSE);
      KaraFree();
      InfoFree();

      MIDIDrop(&MIDIData);
      NextFile();
      PlayDone=FALSE;      
      return;
      }
   else if (Playing||Pausing) { /* Playing or Pausing - display status */
      kp=hp;
      if (kp) {
         MTime=GetCurTime();
         DisplayTime(MTime);
         SetSlider(MTime);
         if (RewindPressed) Rewind();
         if (FFwdPressed) FFwd();
/* Karaoke */
         if (Karaoke) {
            KarPP=KarP;
            KarP=-1;
            do KarP++; while (KarData[KarP].time<MTime);
            KarP--;
            if ((KarP!=KarPP)&&(KarP>=0)) {
               WinSendMsg(hwndKaraMLE, MLM_SETSEL, (MPARAM)KarData[KarP].ipt, (MPARAM)KarData[KarP+1].ipt);
               }
            }
          } 
      if (Pausing) {
         if (Pausing<PAUSE_FOREVER) Pausing--;
         if (!Pausing) {	// Restart playing
            SetPause(FALSE);
            Play_Start();
            }
         }
      return;
      }
   else if (LoadErr) {		/* Loading failed */
      LoadErr=FALSE;
      WinStopTimer(hab,hwndMain,MY_TIMER);
      MessageBox(HWND_DESKTOP, IDMSG_LOADFAILED, "Error !",
                 MB_OK | MB_ERROR | MB_MOVEABLE, TRUE);
      WinStartTimer(hab,hwndMain,MY_TIMER,TIMER_DELAY);
      NextFile();
      return;
      }
   else if (LoadOk) {		/* Loading succeeded */
      LoadOk=FALSE;
// Parse Karaoke
//      Karaoke=Kara;
      MakeInfo();
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_INFO),TRUE);
      if (hwndInfoFrame) CreateInfoWindow();
      KaraParse(0xD);
      if (KarEntries) WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_KARA),TRUE);
      else WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_KARA),FALSE);
      if (CodePage) KaraXlat();

      if (Karaoke) {
         CreateKaraWindow();
         }
      else DestroyKaraWindow();

      sprintf(szTmp,"%u:%02u",(TotalTime/1000)/60,(TotalTime/1000)%60);
      WinSetWindowText(WinWindowFromID(hwndMain,ID_MA_TOTTIME),szTmp);

/* File loaded */
      strcpy(szTmp,szTitle);
      strncat(szTmp," - ",MESSAGELEN);
      strncat(szTmp,pszFileName+FileNo*CCHMAXPATH,MESSAGELEN);
      WinSetWindowText(hwndMain,szTmp);
      if (Files>1) {
         if (Cycle) {
            WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PREV),TRUE);
            WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_NEXT),TRUE);
            }
         else {
            if (FileNo>0) WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PREV),TRUE);
            else WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PREV),FALSE);
            if (FileNo<(Files-1)) WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_NEXT),TRUE);
            else WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_NEXT),FALSE);
            }
         }

// Start playing
      SetPause(FALSE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_SLIDER),TRUE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PAUSE),TRUE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_PREV),TRUE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_REWND),TRUE);
      WinEnableWindow(WinWindowFromID(hwndMain,ID_MA_FFWD),TRUE);

      StartFrom=0;
      Play_Start();
      return;
      }
   else if (!LoadTID) {		// No file loaded, not loading now
//      WinSetWindowText(WinWindowFromID(hwndMain,ID_MA_LIST),pszFileName+FileNo*CCHMAXPATH);
      WinSendMsg(WinWindowFromID(hwndMain,ID_MA_LIST),LM_SELECTITEM,
                 (MPARAM)FileNo,(MPARAM)TRUE ) ;
      strcpy(szTmp,szTitle);
      strcat(szTmp,szLoading);
      WinSetWindowText(hwndMain,szTmp);
      DosCreateThread(&LoadTID,LoadThread,(ULONG)0,0,8192);
      }
   return;
   }

// Main dialog command
void MainCommand(MPARAM mp1) {
   long CurTime=0;

   switch(LOUSHORT(mp1)) {
/*      case ID_MA_UNPAUSE:
         if (Pausing) Pausing=1; */
      case ID_MA_PAUSE:
         if (Pausing) Pausing=1;     // For keyboard control
         else if (Playing) {
            if (hp) CurTime=hp->rtime;
            else CurTime=0;
            StartFrom=CurTime;
            Pausing=PAUSE_FOREVER;
            Play_Stop();
            SetPause(TRUE);
            }
         break;
      case ID_MA_REWND:
         RewindPressed=FALSE;
         Rewind();
         break;
      case ID_MA_FFWD:
         FFwdPressed=FALSE;
         FFwd();
         break;
      case ID_MA_PREV:
         if (Pausing) CurTime=StartFrom;
         else if ((Playing)&&(hp)) CurTime=hp->rtime;
         else CurTime=0;
         if ((Playing||Pausing)&&(CurTime>SKIPSTEP)) {	// Restart playing
            StartFrom=0;
            if (Pausing<PAUSE_FOREVER) Pausing=SKIP_PAUSE;
            SetPause(TRUE);
            Play_Stop();
            }
         else if ((Playing||Pausing)&&(Files>1)&&((FileNo)||(Cycle))) {
            FileNo-=2;
            PlayDone=TRUE;
            Play_Stop();
            Pausing=0;
            WinPostMsg(hwndMain,WM_USER,NULL, NULL);
            }
         break;
      case ID_MA_NEXT:
         if ((Playing||Pausing)&&(Files>1)&&((FileNo<Files-1)||(Cycle))) {
            PlayDone=TRUE;
//            Playing=FALSE;
            Play_Stop();
            Pausing=0;
            WinPostMsg(hwndMain,WM_USER,NULL, NULL);
            }
         break;
      case ID_MA_QUIT:
         Close=TRUE;
         PlayDone=TRUE;
         Play_Stop();
         WinPostMsg(hwndMain,WM_USER,NULL, NULL);
         break;
      case ID_MA_KARA:
         if (!KarEntries) break;
         if (Kara) {
            Kara=Karaoke=FALSE;
            DestroyKaraWindow();
            }
         else {
            Kara=Karaoke=TRUE;
            CreateKaraWindow();
            KarP=-1;
            }
         SetKara(Karaoke);
         break;
      case ID_MA_INFO:
         if (!InfoBuf) break;
         if (hwndInfoFrame) DestroyInfoWindow();
         else CreateInfoWindow();
         SetInfo((int)hwndInfoFrame);
         break;
      }
   return;
   }


MRESULT EXPENTRY MainDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   MRESULT sRC;
   int Setting;
   unsigned long PrevStartFrom;

     switch (msg) {
       case WM_INITDLG:
          WinSendMsg(hwnd,WM_SETICON,
            (MPARAM) WinLoadPointer(HWND_DESKTOP,(HMODULE) NULL,ID_RESOURCE ),
            (MPARAM) 0 );
          WinRestoreWindowPos(szIniApp,szKeyMainWindowPos,hwnd);
 	  break;
       case WM_TIMER:
          if ((int)mp1==MY_TIMER) {	/* My timer */
             MyEvent(hwnd);
             }
	  else {			/* PM's internal timer */
	     sRC = WinDefDlgProc(hwnd, msg, mp1, mp2);
	     return sRC;
	     }
	  break;
       case WM_USER:
          MyEvent(hwnd);
          break;
       case WM_CLOSE:
          Close=TRUE;
          PlayDone=TRUE;
          Play_Stop();
          break;
//       case WM_BUTTON1DOWN:
//          if (LOUSHORT(mp1)==ID_MA_REWND) RewindPressed=TRUE;
//          if (LOUSHORT(mp1)==ID_MA_FFWD) FFwdPressed=TRUE;
            break;
       case WM_COMMAND:
          MainCommand(mp1);
          break;
       case WM_CONTROL:
          switch(LOUSHORT(mp1)) {
           case ID_MA_SLIDER:
             if ((!SlMoving)&&((SHORT2FROMMP(mp1)==SLN_CHANGE)||(SHORT2FROMMP(mp1)==SLN_SLIDERTRACK))) {
                Setting=(int)WinSendDlgItemMsg(hwnd, ID_MA_SLIDER,SLM_QUERYSLIDERINFO,
   		     MPFROM2SHORT(SMA_SLIDERARMPOSITION,SMA_INCREMENTVALUE),
		     (MPARAM)NULL);
                PrevStartFrom=StartFrom;
                StartFrom=TotalTime*Setting/SLIDERTICKS;
                if (PrevStartFrom!=StartFrom) DisplayTime(StartFrom);
                if (Pausing<PAUSE_FOREVER) Pausing=SKIP_PAUSE;
                SetPause(TRUE);
                Play_Stop();
                }          
             break;
           case ID_MA_LIST:
             Setting=(int)WinSendDlgItemMsg(hwnd, ID_MA_LIST,LM_QUERYSELECTION,
		     (MPARAM)NULL,(MPARAM)NULL);
             if ((SHORT2FROMMP(mp1)==CBN_LBSELECT)&&(Setting!=LIT_NONE)&&(Setting!=FileNo)) {
                FileNo=Setting-1;
                PlayDone=TRUE;
                Play_Stop();
                }
             break;
           case ID_MA_REWND:
              if (SHORT2FROMMP(mp1)==BN_PAINT) {
                 DrawButton(mp2,IDB_SCANB);
                 if (((USERBUTTON *)mp2)->fsState&BDS_HILITED) RewindPressed=TRUE;
                 }
              break;
           case ID_MA_FFWD:
              if (SHORT2FROMMP(mp1)==BN_PAINT) {
                 DrawButton(mp2,IDB_SCANF);
                 if (((USERBUTTON *)mp2)->fsState&BDS_HILITED) FFwdPressed=TRUE;
                 }
             break;
           case ID_MA_PREV:
              if (SHORT2FROMMP(mp1)==BN_PAINT)
                 DrawButton(mp2,IDB_SKIPB);
             break;
           case ID_MA_NEXT:
              if (SHORT2FROMMP(mp1)==BN_PAINT)
                 DrawButton(mp2,IDB_SKIPF);
             break;
           case ID_MA_QUIT:
              if (SHORT2FROMMP(mp1)==BN_PAINT)
                 DrawButton(mp2,IDB_EXIT);
             break;
           case ID_MA_PAUSE:
              if (SHORT2FROMMP(mp1)==BN_PAINT) {
                 if (Pausing) DrawButton(mp2,IDB_UNPAUSE);
                 else DrawButton(mp2,IDB_PAUSE);
                 }
              break;
           case ID_MA_KARA:
              if (SHORT2FROMMP(mp1)==BN_PAINT) {
                 if (Karaoke) DrawButton(mp2,IDB_KARAOFF);
                 else DrawButton(mp2,IDB_KARAON);
                 }
             break;
           case ID_MA_INFO:
              if (SHORT2FROMMP(mp1)==BN_PAINT) {
                 if (hwndInfoFrame) DrawButton(mp2,IDB_INFOOFF);
                 else DrawButton(mp2,IDB_INFOON);
                 }
             break;
           }


          break;
       default:	
	  sRC = WinDefDlgProc(hwnd, msg, mp1, mp2);
	  return sRC;
       }
   return (MRESULT)0L;
}

MRESULT EXPENTRY KaraWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   MRESULT sRC;

   switch (msg) {
      case WM_CLOSE:
         Kara=Karaoke=FALSE;
         DestroyKaraWindow();
         SetKara(FALSE);
         return 0;
      case WM_COMMAND:
         MainCommand(mp1);
         break;
      case WM_SIZE:
         WinSetWindowPos(hwndKaraMLE, HWND_TOP,
                          0, 0, SHORT1FROMMP(mp2),SHORT2FROMMP(mp2), SWP_SIZE|SWP_SHOW);
         break;
//      default:
         
      }
   sRC = WinDefWindowProc(hwnd, msg, mp1, mp2);
   return sRC;
}

MRESULT EXPENTRY InfoWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   MRESULT sRC;

   switch (msg) {
      case WM_CLOSE:
         DestroyInfoWindow();
         SetInfo(FALSE);
         return 0;
      case WM_COMMAND:
         MainCommand(mp1);
         break;
      case WM_SIZE:
         WinSetWindowPos(hwndInfoMLE, HWND_TOP,
                          0, 0, SHORT1FROMMP(mp2),SHORT2FROMMP(mp2), SWP_SIZE|SWP_SHOW);
         break;
//      default:
         
      }
   sRC = WinDefWindowProc(hwnd, msg, mp1, mp2);
   return sRC;
}

