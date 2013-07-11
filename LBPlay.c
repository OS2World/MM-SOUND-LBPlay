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
#define INCL_ERRORS
#define INCL_NOPMAPI
#define INCL_DOSMODULEMGR
#define INCL_DOSSEMAPHORES
#define INCL_DOSFILEMGR
#define INCL_DOSQUEUES
#define INCL_DOSPROCESS
#define INCL_DOSMEMMGR
#define INCL_DOSDATETIME
#define INCL_DOSNLS

#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <meerror.h>
#include <mididll.h>
#include <midios2.h>
#include <time.h>

#include "LBPlay.h"

/* Globals */

CHAR    	*pszFileName;		// Ptr to the playlist
int     	Files;			// Files in playlist
int		Cycle=FALSE;
int		Shuffle=FALSE;
char    	Kara=FALSE;	 	// Global Karaoke setting
char            Karaoke;		// Current karaoke setting
struct KDATA    *KarData;
char            *KarBuf;
int		KarEntries=0;
ULONG		TotalTime;
long    	StartFrom=0;
int		ExtPlayer=FALSE;
int		Pausing;
int		Playing;
int		PlayDone;
int		KarP;
char		*Reset=NULL;
char		*PlayParms[MAXPLAYPARMS];
int		CodePage=0;

MIDIDATA	MIDIData;
EVENTHDR	*hp;


static int KaraMode=0;		// 0 - Auto, otherwise meta type

//  Resets
static const char GM_Reset[]={0x7E,0x7F,0x09,0x01,0xF7};
static const char GS_Reset[]={0x41,0x10,0x42,0x12,0x40,0x00,0x7F,0x00,0x41,0xF7};
static const char XG_Reset[]={0x43,0x10,0x4C,0x00,0x00,0x7E,0x00,0xF7};
static const char NO_Reset[]={0xF7};
static char Custom_Reset[MAX_CUST_RESET]={0xF7};

/* Get current time (ms) */
LONG GetCurTime() {
   unsigned long rtime;

   if (!Playing) rtime=StartFrom;
   else rtime=PlayPos();
   if (rtime>TotalTime) rtime=TotalTime;
   return rtime;
   }

/* Parse Karaoke data into KarData and KarBuf arrays. Return number
   of karaoke entries */
int KaraParse(char EOLChar) {
   EVENTHDR *ehp;
   int KaraMeta,NzEntries=0;
   int i,j,l;
   char c;

   KarEntries=0;
   KaraMeta=KaraMode;
   if (!KaraMeta) {
     ehp=MIDIData.pfirst;
     while (ehp) {
     if ((ehp->status==0xFF)&&(ehp->type==5)) {
        KaraMeta=5;
        break;
        }
     ehp=ehp->pnext;
     if (!KaraMeta) KaraMeta=1;
     }

     ehp=MIDIData.pfirst;
     l=0;
     while(ehp) {
        if ((ehp->status==0xFF)&&(ehp->type==KaraMeta)) {
           KarEntries++;
           l+=ehp->len;
           if (ehp->mtime) NzEntries++;
           }
         ehp=ehp->pnext;
         }  
         if (NzEntries<MINKARAENTRIES) {
            Karaoke=FALSE;
            KarEntries=0;
            return 0;
            }
         KarEntries+=2;
         Karaoke=Kara;
         KarData=malloc(sizeof(struct KDATA)*KarEntries);
         KarBuf=malloc(l*3+1);
         ehp=MIDIData.pfirst;
         i=0;
         l=0;
         while(ehp) {
           if ((ehp->status==0xFF)&&(ehp->type==KaraMeta)) {
              if (!i) { 
                 KarData[i].time=ehp->rtime;
                 KarData[i].ipt=l;
                 }
              else if (KarData[i-1].time!=ehp->rtime) {
                 KarData[i].time=ehp->rtime;
                 KarData[i].ipt=l;
                 }
              else i--;
              j=0;
              while (j<ehp->len) {
                 c=ehp->data[j++];
                 if ((c<0x20)||(c=='/')||(c=='@')) KarBuf[l++]=EOLChar;
                 else if (c=='\\') {
                    KarBuf[l++]=EOLChar;
                    KarBuf[l++]=0x20;      /* To trick PM */
                    KarBuf[l++]=EOLChar;
                    }
                 else KarBuf[l++]=c;
                 if (c=='@') {
                    j++;
                    KarBuf[l++]='>';
                    }
                 }
              i++;
              }
           ehp=ehp->pnext;
           }  
        KarData[i].time=TotalTime+1;
        KarData[i].ipt=l;
        KarData[i+1].time=TotalTime+2;
        KarData[i+1].ipt=l;
        KarBuf[l]=0;
        }
//   return KarEntries-2;
   if (KarEntries<2) KarEntries=0;
   else KarEntries-=2;
   return KarEntries;
   }

void KaraFree() {
   if (KarEntries) {
      free(KarData);
      free(KarBuf);
      KarEntries=0;
      }
   }

void SeedRnd() {
   DATETIME  date;

   DosGetDateTime( &date);
   srand( (USHORT)date.hundredths);
   return;
   }

static int FilesAlloc;	// Number of files for which the space is allocated
static int ppc=0;	// Player parameters count

static int Parse(int argc, char *argv[]);


static int ParseFile(char *name) {
   int rc;
   long Size;
   char *Data;
   char *p,*pp;
   FILE *File;
   int ArgsAlloc;
   int args;
   char **argp;


   File=fopen(name,"rb");
   if(!File) return 1;         // Playlist not found

   fseek(File,0,SEEK_END);
   Size=ftell(File);
   fseek(File,0,SEEK_SET);

   Data=(char*) malloc(Size+1);

   if (fread(Data,1,Size,File)!=Size) {
     fclose(File);
     free(Data);
     return 1;
     }
   fclose(File);

   for(p=Data;(p-Data)<Size;p++) {
      if (!*p) *p=0x0D;
      if (*p==0x1A) *p=0;
      }
   Data[Size]=0;

   args=0;
   ArgsAlloc=100;
   argp=(char **)malloc(ArgsAlloc*sizeof(char *));

   p=strtok(Data,"\x0D\x0A");
   while(p) {
      pp=p;
      while ((*pp)&&(*pp<0x21)) pp++;
      if (*pp) {
         if ((args+2)==ArgsAlloc) {		// Leave place for switch parm
            ArgsAlloc+=100;
            argp=(char **)realloc(argp,ArgsAlloc*sizeof(char *));
            }
         argp[args]=pp;
         pp+=strlen(pp)-1;
         while(*pp<0x21) pp--;
         *(pp+1)=0;
         args++;
         if (*argp[args-1]=='-') {		// Allow switch parm to be in the same line
            pp=argp[args-1];
            while ((*pp)&&(*pp>0x20)) pp++;
            if (*pp) {
               *pp++=0;
               while ((*pp)&&(*pp<0x21)) pp++;
               if (*pp) {
                  argp[args]=pp;
                  args++;
                  }
               }
            }
         }
      p=strtok(NULL,"\x0D\x0A");
      }

   if (!args) return 1;
   rc=Parse(args,argp);
   free(argp);
   free(Data);
   return rc;
   }

static int Parse(int argc, char *argv[]) {
   int i,j,cc;
   unsigned char *p;

   for (i=0;i<argc;i++) {
     if (!strcmp(argv[i],"-gm\0"))  Reset=(char *)GM_Reset;
     else if (!stricmp(argv[i],"-gs\0"))  Reset=(char *)GS_Reset;
     else if (!stricmp(argv[i],"-xg\0"))  Reset=(char *)XG_Reset;
     else if (!stricmp(argv[i],"-nr\0"))  Reset=(char *)NO_Reset;
     else if (!stricmp(argv[i],"-cr\0"))  {
        i++;
        if (i==argc) return 1;			// Syntax
        p=strtok(argv[i]," ,.;+");
        j=0;
        while ((p)&&(j<MAX_CUST_RESET-1)) {
           Custom_Reset[j++]=strtol(p,NULL,16);
           p=strtok(NULL," ,.;+");
           }
        Custom_Reset[j]=0xF7;
        Reset=Custom_Reset;
        }
     else if (!stricmp(argv[i],"-cc\0"))  {
        i++;
        if (i==argc) return 1;			// Syntax
        cc=0;
        p=strtok(argv[i]," ,.;+");
        j=0;
        while ((p)&&(j<MAX_CUST_RESET-2)) {
           Custom_Reset[j]=strtol(p,NULL,16);
           p=strtok(NULL," ,.;+");
           if (j>3) cc+=Custom_Reset[j];
           j++;
           }
        Custom_Reset[j++]=128-(cc%128);
        Custom_Reset[j]=0xF7;
        Reset=Custom_Reset;
        }
     else if (!stricmp(argv[i],"-k\0"))  Kara=TRUE;
     else if (!stricmp(argv[i],"-k5\0")) KaraMode=5;
     else if (!stricmp(argv[i],"-k1\0")) KaraMode=1;
     else if (!stricmp(argv[i],"-c\0")) Cycle=TRUE;
     else if (!stricmp(argv[i],"-s\0")) Shuffle=TRUE;
     else if (!stricmp(argv[i],"-cp\0")) {
        i++;
        if (i==argc) return 1;			// Syntax
        CodePage=atoi(argv[i]);
        }
     else if (!stricmp(argv[i],"-p\0")) {
       i++;
       if (i==argc) return 1;			// Syntax
       if ((ppc+2)>=MAXPLAYPARMS) return 1;	// Too many player parms
       PlayParms[ppc]=malloc(strlen(argv[i])+1);
       strcpy(PlayParms[ppc],argv[i]);
       ppc++;
       }
     else if (!stricmp(argv[i],"-@\0")) {
       i++;
       if (i==argc) return 1;			// Syntax
       j=ParseFile(argv[i]);
       if (j) return j;
       }
     else {
        if ((Files+1)==FilesAlloc) {
           FilesAlloc+=10;
           pszFileName=realloc(pszFileName,CCHMAXPATH*FilesAlloc);
           }
        strncpy(pszFileName+Files*CCHMAXPATH, argv[i],CCHMAXPATH-1);
        Files++;
        }
     }
   return 0;
   }

static const char CfgExt[]=CFGEXT;
int CmdParse(int argc, char *argv[]) {
   int rc;
   unsigned char *p;
   char CfgFile[CCHMAXPATH+1];

   Files=0;
   FilesAlloc=10;
   pszFileName=malloc(CCHMAXPATH*FilesAlloc);
   
   strncpy(CfgFile,argv[0],CCHMAXPATH);
   p=strrchr(CfgFile,'.');
   if (p) *p=0;
   strncat(CfgFile,CfgExt,CCHMAXPATH);
   ParseFile(CfgFile);
   
   rc=Parse(argc-1,argv+1);

   PlayParms[ppc]=NULL;
   if (!rc) if (!Files) rc=1;			// No files to play
   return rc;
   }

void ParmsCleanup() {
   int i;
   for (i=0;PlayParms[i];i++) free(PlayParms[i]);
   }

void ShuffleLst(CHAR *pszFileName, int Files) {
   CHAR *pszBuf;
   int i,j,k;

   if (Files<2) return;

   pszBuf=malloc(Files*CCHMAXPATH);
   memset(pszBuf,0,Files*CCHMAXPATH);
   for (i=0;i<Files;i++) {
      k=0;
      do {
         j=rand()%Files;
         k++;
         } while ((pszBuf[j*CCHMAXPATH])&&(k<10));
      if (pszBuf[j*CCHMAXPATH]) for(j=0;pszBuf[j*CCHMAXPATH];j++);
      strcpy(pszBuf+j*CCHMAXPATH,pszFileName+i*CCHMAXPATH);
      }
   memcpy(pszFileName,pszBuf,Files*CCHMAXPATH);
   free(pszBuf);
   return;
   }

int LoadFile(CHAR *pszName) {
   long Size;
   char *Data;
   int rc;
   FILE *File;

   File=fopen(pszName,"rb");
   if(!File) return FILENOTFOUND;

   fseek(File,0,SEEK_END);
   Size=ftell(File);
   fseek(File,0,SEEK_SET);

   Data=(char*) malloc(Size);

   if (fread(Data,1,Size,File)!=Size) {
     fclose(File);
     free(Data);
     return CANTREAD;
     }
   fclose(File);

   rc=MIDIParse(Data,Size,&MIDIData);
   free(Data);
   if (rc) return rc;

   MIDISort(&MIDIData);
   rc=MIDITime(&MIDIData);
   if (rc) return rc;
   MIDIIndex(&MIDIData);
   TotalTime=MIDIData.plast->rtime;
   return 0;
   }


void Play_Start() {

   Pausing=0;
   KarP=-1;
   hp=MIDIData.pfirst;
   PlayDone=FALSE;
   if (!MIDIData.events) return;
   MIDIRescan(&MIDIData,MIDIRtoM(&MIDIData,StartFrom));
   PlayStart(&MIDIData,StartFrom);
   Playing=TRUE;
   return;
   }

void Play_Stop() {
   PlayStop();
   Playing=FALSE;
   return;
   }

void PlaySet() {
   if (Playing) {
      hp=PlayEvent();
      if (!hp) hp=MIDIPrevR(&MIDIData,PlayPos());
      if (!PlayIsPlaying()) {
         Playing=FALSE;
         PlayDone=TRUE;
         }
      }
   }


