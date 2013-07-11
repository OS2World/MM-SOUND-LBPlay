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
#define INCL_BASE
#define INCL_KBD
#define INCL_ERRORS
#define INCL_NOPMAPI
#define INCL_DOSMODULEMGR
#define INCL_DOSSEMAPHORES
#define INCL_DOSFILEMGR
#define INCL_DOSQUEUES
#define INCL_DOSPROCESS
#define INCL_DOSMEMMGR
#define INCL_DOSDATETIME

#include <os2.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <meerror.h>
#include <mididll.h>
#include <midios2.h>
#include <fcntl.h>
#include "LBplay.h"


#define  INDICPAUSE     5       // Indication pause (xCTLPAUSE)
#define  KARLEN         45      // Karaoke line length
#define  SKIPWAIT       10      /* Time to wait for keystroke if skipping
                                   (xCTLPAUSE) */
#define PAUSE_FOREVER	1000	// Value that is put in Pausing to indicate
				// "Pause until unpaused"

void help() {
   printf("   Usage: LBPLAY <File/Playlist> [File/Playlist] ... [switch] [switch] ...\n");
   printf("          <File/Playlist> is either MIDI file name or -@ Playlist name.\n");
   printf("          -k    - Karaoke on.\n");
   printf("          -k1   - Karaoke using only metaevent type 1 (text).\n");
   printf("          -k5   - Karaoke using only metaevent type 5 (lyric).\n");
   printf("          -gm   - Send GM reset before playing.\n");
   printf("          -gs   - Send GS reset before playing.\n");
   printf("          -xg   - Send XG reset before playing.\n");
   printf("          -nr   - Do not send any reset before playing.\n");
   printf("          -c    - Cycle the playlist.\n");
   printf("          -s    - Shuffle the playlist.\n");
   exit(0);
   }

static char *CfgInfo;

char ManufNone[]="Unknown";
char ManufID[]={1,2,3,4,5,6,7,8,9,0xA,0xB,0xC,0xD,0xE,0x10,0x11,0x12,0x13,
                   0x14,0x1B,0x15,0x16,0x17,0x18,0x20,0x21,0x23,0x24,0x25,0x26,
                   0x27,0x28,0x29,0x2F,0x40,0x41,0x42,0x43,0x44,0x45,0x7D,0x7E,0};
char *Manufs[]={"Sequential Circuits","Big Briar","Octave/Plateau","Moog",
                  "Passport Designs","Lexicon","Kurzweil","Fender","Gulbransen",
                  "Delta Labs","Sound Comp","General Electro","Techmar",
                  "Matthews Research","Oberheim","PAIA","Simmons","Gentle Electric",
                  "Fairlight","Peavey","JL Cooper","Lowery","Lin","Emu","Bon Tempi",
		  "S.I.E.L","SyntheAxe","Hohner","Crumar","Solton","Jellinghaus MS",
	          "CTS","PPG","Elka","Kawai","Roland","Korg","Yamaha","Casio","Akai",
		  "Educational use","General MIDI"};

/* Calculate and display file info */
void FileInfo() {
   int cports=0;
   char Manufacturer=0;
   char *MPtr;
   int i;
   int PortsUsed[256];
                  
   for (i=0;i<256;i++) PortsUsed[i]=FALSE;

   hp=MIDIData.pfirst;
   while (hp) {
      if ((hp->status==0xFF)&&(hp->type==0x21)&&(hp->len>0))
         PortsUsed[hp->data[0]]=TRUE;
      if (hp->status==0xF0) Manufacturer=hp->data[0];
      hp=hp->pnext;
      }

   for (i=0;i<256;i++) if (PortsUsed[i]) cports++;
   if (!cports) cports++;

   if (Manufacturer) {
       for (i=0;(ManufID[i])&&(ManufID[i]!=Manufacturer);i++);
       if (!ManufID[i]) MPtr=ManufNone;
       else MPtr=Manufs[i];
       }
   else MPtr=ManufNone;
   if (!MPtr) MPtr=ManufNone;
   printf("      Fmt: %u, Trks: %u, Evts: %u, Ports: %u, Dur: %u:%02u, Synth: %s.\n",
           MIDIData.fmt,MIDIData.trks,MIDIData.events,cports,(TotalTime/1000)/60,(TotalTime/1000)%60,MPtr);
   return;
   }


int main (INT argc, CHAR *argv[])
{

   int               FileNo;			/* File being played */
   ULONG             i = 0;                        // index
   ULONG             rc = 0;                       // return code
   ULONG CurTime=0;

   int IndicCnt;
   int c,ce;

   long			Tempo,MTime=0;
   EVENTHDR *kp;
   char KarLine[KARLEN];
   int j,l;
//   int KarEntries
   int KarPP;
   KBDKEYINFO kbdkeyinfo;


// OUTLINE: Parse params

   setbuf( stdout, 0 );
   printf("\nLBPLAY v.0.07 by Lesha Bogdanow. Use it FREELY. NO WARRANTY.\n");

   if (CmdParse(argc,argv)) help();

// OUTLINE: Init RTMIDI
   printf("   Initializing Player ... ");

   CfgInfo=malloc(CFGINFOLEN);
   rc=PlayInit(PlayParms,Reset,CfgInfo,CFGINFOLEN);

   printf("%s",CfgInfo);

   if (rc) {
      printf("Failed, rc=%u.\n",rc);
      exit(1);
      }

   

   SeedRnd();

   if (Shuffle) ShuffleLst(pszFileName,Files);
   FileNo=0;
   do {
      if (Files>1) printf("\r   Loading file %u/%u: %s ... ",FileNo+1,Files,pszFileName+FileNo*CCHMAXPATH);
      else printf("\r   Loading file: %s ... ",pszFileName+FileNo*CCHMAXPATH);

     rc=LoadFile(pszFileName+FileNo*CCHMAXPATH);
     if (rc) {
       printf("Failed.\n");
       exit(1);
       }

     printf("Done.\n");

     if (MIDIData.warns&MW_BADTEMPO) printf("      Warning: Bad tempo metaevent.\n");
     if (MIDIData.warns&MW_BADMTHD) printf("      Warning: Bad MThd chunk.\n");
     if (MIDIData.warns&MW_EXTRAMTHD) printf("      Warning: Extra MThd chunk.\n");
     if (MIDIData.warns&MW_BADFMT0) printf("      Warning: More than 1 track in fmt 0.\n");
     if (MIDIData.warns&MW_BADSTATUS) printf("      Warning: Bad message status.\n");
     if (MIDIData.warns&MW_BADRSTATUS) printf("      Warning: Bad running status.\n");
     if (MIDIData.warns&MW_BADMTRK) printf("      Warning: Bad MTrk chunk.\n");
     if (MIDIData.warns&MW_BADCHUNK) printf("      Warning: Bad chunk.\n");
     if (MIDIData.warns&MW_NOMTHD) printf("      Warning: No MThd chunk.\n");
     if (MIDIData.warns&MW_BADTRKCNT) printf("      Warning: Track number mismatch.\n");
     if (MIDIData.warns&MW_BADTIMESIGN) printf("      Warning: Bad time signature metaevent.\n");

     FileInfo();
     KaraParse('|');
     printf("   [Esc] - Exit, [Space] - Pause, [<-][->] - scan, [Up][Down] - skip");
     if (KarEntries) printf(", [K].\n");
     else printf(".\n");
// OUTLINE: PLAY
      StartFrom=0;
      PlayDone=FALSE;
      while(!PlayDone) {
         KbdFlushBuffer(0);
         Play_Start();
         IndicCnt=1;
         while((Playing)||(Pausing)) {
            PlaySet();
     
/*   Indication part */

         kp=hp;
         if ((kp)&&(Playing)&&(!(--IndicCnt)))  {

           IndicCnt=INDICPAUSE;
           MTime=GetCurTime();
           if (Karaoke) {
              KarPP=KarP;
              KarP=-1;
              do KarP++; while (KarData[KarP].time<MTime);
              KarP--;
              if (KarP<0) KarP=0;
              if (KarP!=KarPP) {
                 memset(KarLine,' ',KARLEN-1);
                 KarLine[KARLEN-1]=0;
                 j=KarData[KarP].ipt;
                 l=strlen(KarBuf+j);
                 if (l>(KARLEN-1)) l=KARLEN-1;
                 for (i=0;i<l;i++) KarLine[i]=KarBuf[i+j];
                 }
              printf("\r   Pos: %3u:%02u, Karaoke: %s",(MTime/1000)/60,(MTime/1000)%60,KarLine);
              }
           else if (kp) {
             Tempo=120;
             while ((kp)&&((kp->status!=0xFF)||(kp->type!=0x51))) kp=kp->pprev;
             if (kp) Tempo=60000000/((kp->data[0]<<16)+(kp->data[1]<<8)+kp->data[2]);
             printf("\r   Playing: Pos: %3u:%02u. Tempo: %3ubpm.  ",(MTime/1000)/60,(MTime/1000)%60,Tempo);
             }
           }

/*   Control part */

            KbdCharIn(&kbdkeyinfo,IO_NOWAIT,0);

            if (kbdkeyinfo.fbStatus) {
               c=kbdkeyinfo.chChar;
               if (c==0xE0) c=0;
               if (!c) ce=kbdkeyinfo.chScan;
               else ce=0;
               if (!Pausing) CurTime=MTime;
               if ((CurTime<StartFrom)||(Pausing)) CurTime=StartFrom;
               if (c==27) {
                 Play_Stop();
                 PlayDone=TRUE;
                 Pausing=0;
                 FileNo=Files+1;
                 }
               if ((c=='k')||(c=='K')) {
                 if (KarEntries) {
                    Kara=!Kara;
                    Karaoke=Kara;
                    KarP=-1;
// Clear line
                    memset(KarLine,' ',KARLEN-1);
                    KarLine[KARLEN-1]=0;
                    printf("\r   Pos: %3u:%02u, Karaoke: %s",(MTime/1000)/60,(MTime/1000)%60,KarLine);
                    }
                 }
               if (c==0x20) {
                 if (!Pausing) {
//                    Playing=FALSE;
                    Play_Stop();
                    StartFrom=CurTime;
                    Pausing=PAUSE_FOREVER;
                    printf("\r                                                                       ");
                    printf("\r   Paused at %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                    }
                 else Pausing=1;
                 }
/*               else if (ce==0x47) { // Home
//                 Playing=FALSE;
                 Play_Stop();
                 StartFrom=0;
                 if (Pausing<PAUSE_FOREVER) Pausing=SKIPWAIT;
                 printf("\r                                                                       ");
                 printf("\r   Scanning to %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                 } */
               else if (ce==0x4B) { /* <- */
//                 Playing=FALSE;
                 Play_Stop();
                 if (CurTime>SKIPSTEP) StartFrom=CurTime-SKIPSTEP;
                 else StartFrom=0;
                 printf("\r                                                                       ");
                 if (Pausing<PAUSE_FOREVER) {
                    Pausing=SKIPWAIT;
                    printf("\r   Scanning to %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                    }
                 else printf("\r   Paused at %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                 }
               else if (ce==0x4D) { /* -> */
//                 Playing=FALSE;
                 Play_Stop();
                 StartFrom=CurTime+SKIPSTEP;
                 if (StartFrom>=TotalTime) StartFrom=CurTime;
                 printf("\r                                                                       ");
                 if (Pausing<PAUSE_FOREVER) {
                    Pausing=SKIPWAIT;
                    printf("\r   Scanning to %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                    }
                 else printf("\r   Paused at %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                 }
               else if (ce==0x50) { /* down */
                 if ((Files>1)&&((FileNo<Files-1)||(Cycle))) {
//                    Playing=FALSE;
                    Play_Stop();
                    PlayDone=TRUE;
                    Pausing=0;
                    }
                 }
               else if (ce==0x48) { /* up */
                  if (Pausing) CurTime=StartFrom;
                  else if ((Playing)&&(hp)) CurTime=hp->rtime;
                  else CurTime=0;
                 if ((Playing||Pausing)&&(CurTime>SKIPSTEP)) {	// Restart playing
//                    Playing=FALSE;
                    Play_Stop();
                    StartFrom=0;
                    printf("\r                                                                       ");
                    if (Pausing<PAUSE_FOREVER) {
                       Pausing=SKIPWAIT;
                       printf("\r   Scanning to %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                       }
                    else printf("\r   Paused at %u:%02u ... ",StartFrom/60000,(StartFrom/1000)%60);
                    }
                 else if ((Files>1)&&((FileNo)||(Cycle))) {
//                    Playing=FALSE;
                    Play_Stop();
                    PlayDone=TRUE;
                    Pausing=0;
                    FileNo-=2;
                    }
                 }
               }
            else {
               DosSleep(CTLPAUSE);
               if ((Pausing)&&(Pausing<PAUSE_FOREVER)) Pausing--;
//               if (Pausing>SKIPWAIT) Pausing=0;
               }
            } // end while
         Play_Stop();
         }
      printf("\r                                                                       ");
      printf("\r   Cleaning up ... ");

      KaraFree();
      MIDIDrop(&MIDIData);
      FileNo++;
      if (Cycle) {
        if (FileNo<0) FileNo=Files-1;
        if (FileNo==Files) {
           FileNo=0;
           if (Shuffle) ShuffleLst(pszFileName,Files);
           }
        }
      } while ((FileNo<Files)&&(FileNo>=0));

// OUTLINE: CLEANUP

   KbdFlushBuffer(0);
   rc=PlayCleanup();
   if (rc) {
      printf("Failed, rc=%u.\n",rc);
      exit(1);
      }
   ParmsCleanup();
   printf("Done.\n");

   DosExit( EXIT_PROCESS, 0 );
   return 0;		// Just to avoid warning 
} /* end main */

