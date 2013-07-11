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
#define INCL_DOSFILEMGR
#define INCL_DOSDEVIOCTL
#define INCL_DOSDEVICES
#include <os2.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys\hw.h>


int MPUInit(USHORT BasePort) {
  _portaccess(BasePort,BasePort+1);
  _outp8(BasePort+1,0xFF);
  if (_inp8(BasePort)!=0xFE) return FALSE;
  _outp8(BasePort+1,0x3F);
  if (_inp8(BasePort)!=0xFE) return FALSE;
  return TRUE;
  }


int main(INT argc, CHAR *argv[]) {
   USHORT BasePort;
   int i;

   printf("\nMPU-401 INIT v.0.01 by Lesha Bogdanow. Use it FREELY. NO WARRANTY.\n");

   for (i=1;i<argc;i++) {
      BasePort=strtoul(argv[i],NULL,16);
      printf("   Initializing MPU-401 at %x ... ",BasePort);
      if (!MPUInit(BasePort)) printf("failed.\n");
      else printf("done.\n");
      }

   return 0;
   }
