/*
//
// Copyright (C) 2006-2021 Jean-Fran�ois DEL NERO
//
// This file is part of HxCFloppyEmulator.
//
// HxCFloppyEmulator may be used and distributed without restriction provided
// that this copyright statement is not removed from the file and that any
// derivative work contains the original copyright notice and the associated
// disclaimer.
//
// HxCFloppyEmulator is free software; you can redistribute it
// and/or modify  it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// HxCFloppyEmulator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with HxCFloppyEmulator; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
*/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>

extern "C"
{
	#include "libhxcfe.h"
	#include "usb_hxcfloppyemulator.h"
	#include "version.h"
}

#include "fl_includes.h"

#include "about_gui.h"
#include "license_gui.h"

#include "main.h"
#include "gui_strings.h"

extern s_gui_context * guicontext;

static char buffer2[1024*16];
static char buffer1[1024*16];

int audiostarted;
int demostate;
uintro_context * gb_ui_context;

#ifdef WIN32
static HWAVEOUT shwd;
static WAVEFORMATEX pwfx;
static WAVEHDR pwhOut1;
static WAVEHDR pwhOut2;

void CALLBACK SoundHandlerProc(HWAVEOUT hwo,UINT uMsg,DWORD * dwInstance,DWORD *  dwParam1,DWORD *  dwParam2)
{
	if(audiostarted)
	{
		switch(uMsg)
		{
			case WOM_OPEN:
				break;
			case WOM_DONE:
				uintro_getnext_soundsample(gb_ui_context,(short*)((struct wavehdr_tag *)dwParam1)->lpData,sizeof(buffer2)/2);
				waveOutWrite(shwd,(struct wavehdr_tag *)dwParam1,sizeof(pwhOut2));
				break;
			case WOM_CLOSE:
				break;
		}
	}
	return;
}
#endif

int startAudioOut()
{
	if(!audiostarted)
	{
		memset(buffer1,0,sizeof(buffer1));
		memset(buffer2,0,sizeof(buffer2));

		#ifdef WIN32

		if(waveOutGetNumDevs()!=0)
		{
			pwfx.wFormatTag=1;
			pwfx.nChannels=2;
			pwfx.nSamplesPerSec=44100;
			pwfx.nAvgBytesPerSec=pwfx.nSamplesPerSec*4;
			pwfx.nBlockAlign=4;
			pwfx.wBitsPerSample=16;
			pwfx.cbSize=0;

			waveOutOpen(&shwd,WAVE_MAPPER,&pwfx,(DWORD_PTR)&SoundHandlerProc,0,CALLBACK_FUNCTION);
			pwhOut1.lpData=(char*)buffer1;
			pwhOut1.dwBufferLength=sizeof(buffer1);
			pwhOut1.dwFlags=0;
			pwhOut1.dwLoops=0;

			pwhOut2.lpData=(char*)buffer2;
			pwhOut2.dwBufferLength=sizeof(buffer2);
			pwhOut2.dwFlags=0;
			pwhOut2.dwLoops=0;

			waveOutPrepareHeader(shwd, &pwhOut1, sizeof(pwhOut1));
			waveOutPrepareHeader(shwd, &pwhOut2, sizeof(pwhOut2));

			waveOutWrite(shwd,&pwhOut1,sizeof(pwhOut1));
			waveOutWrite(shwd,&pwhOut2,sizeof(pwhOut2));

			audiostarted = 1;
		}

		#endif
	}

	return 0;
}

int stopAudioOut()
{

	if(audiostarted)
	{
		audiostarted = 0;

		#ifdef WIN32
		waveOutReset(shwd);
		waveOutBreakLoop(shwd);
		waveOutBreakLoop(shwd);

		waveOutUnprepareHeader(shwd,&pwhOut1,sizeof(WAVEHDR));
		waveOutUnprepareHeader(shwd,&pwhOut2,sizeof(WAVEHDR));

		waveOutClose(shwd);
		#endif
	}

	return 0;
}

static void tick(void *v)
{
	About_box *window;
	unsigned char * ptr1;
	int i,j,k;

	window=(About_box *)v;

	if(window->shown())
	{
		demostate = 1;
		startAudioOut();
		window->make_current();
		uintro_getnextframe(window->ui_context);

		ptr1=(unsigned char*)window->ui_context->framebuffer;
		k=0;
		j=0;
		for(i=0;i<window->xsize*window->ysize;i++)
		{
			ptr1[j++]=ptr1[k+2];
			ptr1[j++]=ptr1[k+1];
			ptr1[j++]=ptr1[k+0];
			k=k+4;
		}

		fl_draw_image((unsigned char *)window->ui_context->framebuffer, window->xpos_size, window->ypos_size, window->xsize, window->ysize, 3, 0);
	}
	else
	{
		if(demostate)
		{
			demostate = 0;
			uintro_reset(window->ui_context);
		}
		stopAudioOut();
	}


	Fl::repeat_timeout(0.02, tick, v);
}

void close(Fl_Widget *w, void * t)
{
	stopAudioOut();
	w->parent()->hide();
}

void create_license_window(Fl_Widget *, void *)
{
	new License_box();
	return ;
}

void OpenURLInBrowser(Fl_Widget *,void* u)
{
	#if defined (WIN32)
		char * url;
		url=(char*)u;

		ShellExecute(HWND_DESKTOP, "open", url, NULL, NULL, SW_SHOW);
	#elif defined (OSX)
		char * url;
		char commandString[2048];

		url=(char*)u;

		sprintf(commandString, "open %s", url);
		system(commandString);
	#elif defined (__amigaos4__)
		char * url;
		char commandString[2048];

		url=(char*)u;

		sprintf(commandString, "ibrowse:ibrowse %s", url);
		system(commandString);
	#endif
}

About_box::~About_box()
{
	Fl::remove_timeout(tick,0);

	stopAudioOut();

    uintro_deinit(this->ui_context);
}

#define BUTTONS_BLOCK_XPOS 5
#define BUTTONS_BLOCK_YPOS 100

#define BUTTON_SIZE_X 180
#define BUTTON_SIZE_Y 25

About_box::About_box()
  : Fl_Window(530,240)
{
	int cur_xpos,cur_ypos;

	o = new Fl_Box(5, 5, BUTTON_SIZE_X, BUTTON_SIZE_Y, "HxC Floppy Emulator");
	o->box(FL_DOWN_BOX);

	cur_xpos = BUTTONS_BLOCK_XPOS;
	cur_ypos = BUTTONS_BLOCK_YPOS;

	button_wesite = new Fl_Button(cur_xpos, cur_ypos, BUTTON_SIZE_X, BUTTON_SIZE_Y, "Website");
	button_wesite->callback(OpenURLInBrowser,(void*)"https://hxc2001.com/");

	cur_ypos += BUTTON_SIZE_Y;

	button_forum = new Fl_Button(cur_xpos, cur_ypos, BUTTON_SIZE_X / 2, BUTTON_SIZE_Y, "Forum");
	button_forum->callback(OpenURLInBrowser,(void*)"http://torlus.com/floppy/forum");

	button_facebook = new Fl_Button(cur_xpos + (BUTTON_SIZE_X / 2), cur_ypos, BUTTON_SIZE_X / 2, BUTTON_SIZE_Y, "Facebook");
	button_facebook->callback(OpenURLInBrowser,(void*)"https://www.facebook.com/groups/hxc2001/");

	cur_ypos += BUTTON_SIZE_Y;

	button_releasenotes = new Fl_Button(cur_xpos, cur_ypos, BUTTON_SIZE_X, BUTTON_SIZE_Y, "Latest release notes");
	button_releasenotes->callback(OpenURLInBrowser,(void*)"https://hxc2001.com/download/floppy_drive_emulator/hxcfloppyemulator_soft_release_notes.txt");

	cur_ypos += BUTTON_SIZE_Y;

	button_license = new Fl_Button(cur_xpos, cur_ypos, BUTTON_SIZE_X, BUTTON_SIZE_Y, "Under GPL License");
	button_license->callback(create_license_window,0);

	cur_ypos += BUTTON_SIZE_Y;

	button_ok = new Fl_Button(cur_xpos, cur_ypos, BUTTON_SIZE_X / 2, BUTTON_SIZE_Y, getString(STR_COMMON_OK) ); // Fl_Button* o
	button_ok->callback(close,0);

	o = new Fl_Box(200, 5, 320+6, 200+6);
	o->box(FL_UP_BOX);// Fl_Box* o

	o = new Fl_Box(200, 200+6 + 5, 320+6, BUTTON_SIZE_Y, "hxc2001.com Mail: hxc2001(at)hxc2001.com");
	o->box(FL_DOWN_BOX);

	o = new Fl_Box(5, 35, 180, 60, "Copyright (c) 2006-2021\nJean-François DEL NERO\n(c) HxC2001");
	o->box(FL_DOWN_BOX);

	xpos_size=200+3;
	ypos_size=5+3;

	xsize=320;
	ysize=200;

	ui_context=uintro_init(xsize,ysize);
	gb_ui_context = ui_context;

	this->end();

	windowname[0]=0;
	strcpy(windowname,NOMFENETRE" - libhxcfe v");
	strcat(windowname,hxcfe_getVersion(guicontext->hxcfe));

	this->label(windowname);

	audiostarted = 0;
	demostate = 0;

	Fl::add_timeout( 0.02, tick, (void*)this);

	return;
}
