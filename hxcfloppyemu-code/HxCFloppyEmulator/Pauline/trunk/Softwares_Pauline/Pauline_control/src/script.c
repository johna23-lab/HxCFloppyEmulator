/*
//
// Copyright (C) 2019-2021 Jean-François DEL NERO
//
// This file is part of the Pauline control software
//
// Pauline control software may be used and distributed without restriction provided
// that this copyright statement is not removed from the file and that any
// derivative work contains the original copyright notice and the associated
// disclaimer.
//
// Pauline control software is free software; you can redistribute it
// and/or modify  it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// Pauline control software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Pauline control software; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <dirent.h>
#include <errno.h>

#include "libhxcfe.h"

#include "script.h"
#include "fpga.h"
#include "network.h"
#include "errors.h"

#include "bmp_file.h"
#include "screen.h"

#include "messages.h"

#include "utils.h"

#include "version.h"

extern char home_folder[512];
extern fpga_state * fpga;
typedef int (* CMD_FUNC)(script_ctx * ctx, char * line);

extern char file_to_analyse[];
extern char last_file_to_analyse[];

PRINTF_FUNC script_printf;

typedef struct cmd_list_
{
	char * command;
	CMD_FUNC func;
}cmd_list;

#define ERROR_CMD_NOT_FOUND -10

volatile int dump_time_per_track;
volatile int index_to_dump_delay;

pthread_t     threads_dump;
char thread_dump_cmdline[512];

volatile int dump_running = 0;
volatile int dump_drive = 0;
volatile int stop_process = 0;

extern volatile int preview_image_flags;
extern volatile int preview_image_xtime;
extern volatile int preview_image_xoffset;
extern volatile int preview_image_ytime;

extern char file_to_analyse[512];
extern char last_file_to_analyse[512];

script_ctx * script_context = NULL;

script_ctx * pauline_init_script()
{
	script_ctx * ctx;

	ctx = malloc(sizeof(script_ctx));

	if(ctx)
	{
		memset(ctx,0,sizeof(script_ctx));
		pauline_setOutputFunc( ctx, msg_printf );
		pthread_mutex_init(&ctx->script_mutex, NULL);
	}

	script_context = ctx;

	return ctx;
}

script_ctx * pauline_deinit_script(script_ctx * ctx)
{
	if(ctx)
	{
		free(ctx);
	}

	return ctx;
}

void pauline_setOutputFunc( script_ctx * ctx, PRINTF_FUNC ext_printf )
{
	ctx->script_printf = ext_printf;

	return;
}

static int is_end_line(char c)
{
	if( c == 0 || c == '#' || c == '\r' || c == '\n' )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static int is_space(char c)
{
	if( c == ' ' || c == '\t' )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static int get_next_word(char * line, int offset)
{
	while( !is_end_line(line[offset]) && ( line[offset] == ' ' ) )
	{
		offset++;
	}

	return offset;
}

static int copy_param(char * dest, char * line, int offs)
{
	int i,insidequote;

	i = 0;
	insidequote = 0;
	while( (!is_end_line(line[offs]) || (insidequote && line[offs] == '#') ) && ( insidequote || !is_space(line[offs]) ) && (i < (DEFAULT_BUFLEN - 1)) )
	{
		if(line[offs] != '"')
		{
			if(dest)
				dest[i] = line[offs];

			i++;
		}
		else
		{
			if(insidequote)
				insidequote = 0;
			else
				insidequote = 1;
		}

		offs++;
	}

	if(dest)
		dest[i] = 0;

	return offs;
}

static int get_param_offset(char * line, int param)
{
	int param_cnt, offs;

	offs = 0;
	offs = get_next_word(line, offs);

	param_cnt = 0;
	do
	{
		offs = copy_param(NULL, line, offs);

		offs = get_next_word( line, offs );

		if(line[offs] == 0 || line[offs] == '#')
			return -1;

		param_cnt++;
	}while( param_cnt < param );

	return offs;
}

static int get_param(char * line, int param_offset,char * param)
{
	int offs;

	offs = get_param_offset(line, param_offset);

	if(offs>=0)
	{
		offs = copy_param(param, line, offs);

		return 1;
	}

	return -1;
}

static int is_dir_present(char * path)
{
	DIR* dir = opendir(path);

	if (dir)
	{
		closedir(dir);
		return 1;
	}
	else
	{
		if (ENOENT == errno)
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
}

static char forbiddenfilenamechars[]={'\\','/',':','`','*','|',';','"','\'',0};
static char global_search_name[512];
static int global_max_index;

static int display_info(const char *fpath, const struct stat *sb, int tflag)
{
	int i,j,val;

	if(tflag == FTW_D)
	{
		i = strlen(fpath);
		while( (fpath[i] != '/') && i)
		{
			i--;
		}

		if(  fpath[i] == '/' )
			i++;

		if( !strncmp( &fpath[i],global_search_name,strlen(global_search_name)) )
		{
			if( strlen(&fpath[i]) - strlen(global_search_name) == 4 )
			{
				i += strlen(global_search_name);

				for(j=0;j<4;j++)
				{
					if(  !(fpath[i + j] >= '0' && fpath[i + j] <= '9') )
					{
						break;
					}
				}

				if( j == 4 )
				{
					val = atoi(&fpath[i]);

					if(val > global_max_index)
					{
						global_max_index = val;
					}
				}
			}
		}

	}

	return 0;
}

static int is_valid_char(char c)
{
	int i;

	i =0;
	while(forbiddenfilenamechars[i])
	{
		if(forbiddenfilenamechars[i] == c)
		{
			return 0;
		}
		i++;
	}

	return 1;
}

static int prepare_folder(char * name, char * comment, int start_index, int mode, char * folder_pathoutput)
{
	char folder_path[1024];
	char dump_name[512];
	char tmp[512];
	struct stat st = {0};
	int ret;
	int max;
	int i;

	ret = -1;

	strcpy(dump_name,"untitled dump");
	if(name)
	{
		i = strlen(name);
		if(i)
		{
			strcpy(dump_name,name);
			while(i)
			{
				if(dump_name[i - 1] == ' ')
				{
					dump_name[i - 1] = '\0';
				}
				else
				{
					break;
				}

				i--;
			}

			i = 0;
			while(dump_name[i])
			{
				if(!is_valid_char(dump_name[i]))
				{
					dump_name[i] = '_';
				}
				i++;
			}
		}
	}

	if(comment)
	{
		i = strlen(comment);
		if(i)
		{
			while(i)
			{
				if(comment[i - 1] == ' ')
				{
					comment[i - 1] = '\0';
				}
				else
				{
					break;
				}

				i--;
			}
		}

		i = 0;
		while(comment[i])
		{
			if(!is_valid_char(comment[i]))
			{
				comment[i] = '_';
			}
			i++;
		}
	}

	switch(mode)
	{
		case 0:
			sprintf(folder_path,"%s/%s",home_folder,dump_name);

			if(!is_dir_present(folder_path))
			{
				ret = mkdir(folder_path, 0777);
			}

			if(strlen(comment))
			{
				sprintf(tmp,"/%s-%s",dump_name,comment);
			}
			else
			{
				sprintf(tmp,"/%s",dump_name);
			}

			strcat(folder_path,tmp);

			if(is_dir_present(folder_path))
			{
				sprintf(tmp,"rm -rf \"%s\"",folder_path);
				ret = system(tmp);
			}

			if (stat(folder_path, &st) == -1)
			{
				ret = mkdir(folder_path, 0777);
			}

			strcpy(folder_pathoutput,folder_path);

			// No file index
		break;
		case 1:
			// Manual - no auto increment (overwrite if present)
			sprintf(folder_path,"%s/%s",home_folder,dump_name);

			if(!is_dir_present(folder_path))
			{
				ret = mkdir(folder_path, 0777);
			}

			if(strlen(comment))
			{
				sprintf(tmp,"/%s-%s-%.4d",dump_name,comment,start_index);
			}
			else
			{
				sprintf(tmp,"/%s-%.4d",dump_name,start_index);
			}
			strcat(folder_path,tmp);

			if(is_dir_present(folder_path))
			{
				sprintf(tmp,"rm -rf \"%s\"",folder_path);
				ret = system(tmp);
			}

			if (stat(folder_path, &st) == -1)
			{
				ret = mkdir(folder_path, 0777);
			}

			strcpy(folder_pathoutput,folder_path);

		break;
		case 2:
			sprintf(folder_path,"%s/%s",home_folder,dump_name);

			if(!is_dir_present(folder_path))
			{
				ret = mkdir(folder_path, 0777);
				if(ret < 0)
					return -1;
			}

			// Find the last index (no overwrite!)
			global_max_index = 0;
			if(strlen(comment))
			{
				sprintf(global_search_name,"%s-%s-",dump_name,comment);
			}
			else
			{
				sprintf(global_search_name,"%s-",dump_name);
			}

			ftw(folder_path, display_info, 20 );

			max = global_max_index;

			if(max != 9999)
			{
				max++;

				if(max < start_index)
					max = start_index;

				if(strlen(comment))
				{
					sprintf(tmp,"/%s-%s-%.4d",dump_name,comment,max);
				}
				else
				{
					sprintf(tmp,"/%s-%.4d",dump_name,max);
				}
				strcat(folder_path,tmp);

				if(!is_dir_present(folder_path))
				{
					ret = mkdir(folder_path, 0777);
					strcpy(folder_pathoutput,folder_path);
					ret = 0;
				}
			}
		break;
	}

	return ret;
}

static int readdisk(int drive, int dump_start_track,int dump_max_track,int dump_start_side,int dump_max_side,int high_res_mode,int doublestep,int ignore_index,int spy, char * name, char * comment, char * comment2, int start_index, int incmode, char * driveref, char * operator)
{
	int i,j,max_track;
	char temp[512];
	char folder_path[512];

	FILE *f;
	unsigned char * tmpptr;
	uint32_t  buffersize;
	int error;
	char * var;
	dump_state state;

	error = PAULINE_NO_ERROR;

	f = NULL;
	dump_running = 1;
	dump_drive = drive;
	if(!spy)
	{
		msg_printf(MSGTYPE_INFO_0,"Start disk reading...\nTrack(s): %d <-> %d, Side(s): %d <-> %d, Ignore index: %d, Time: %dms, %s\n",dump_start_track,dump_max_track,dump_start_side,dump_max_side,ignore_index,dump_time_per_track,high_res_mode?"50Mhz":"25Mhz");

		sound(fpga, 1000, 50);

		memset(&state,0,sizeof(dump_state));

		state.drive_number = drive;
		state.start_track = dump_start_track;
		state.max_track = dump_max_track;
		state.start_side = dump_start_side;
		state.max_side = dump_max_side;

		if(ignore_index)
			state.index_synced = 0;
		else
			state.index_synced = 1;

		state.time_per_track = dump_time_per_track;
		state.doublestep = doublestep;
		state.index_to_dump_delay = index_to_dump_delay;
		strncpy( (char*)&state.dump_name, name, 512 - 1);
		strncpy( (char*)&state.dump_comment, comment , 512 - 1 );
		strncpy( (char*)&state.dump_comment2, comment2 , 512 - 1 );
		strncpy( (char*)&state.dump_driveref, driveref , 512 - 1 );
		strncpy( (char*)&state.dump_operator, operator , 512 - 1 );

		sprintf(temp,"DRIVE_%d_DESCRIPTION",drive);
		var = hxcfe_getEnvVar( fpga->libhxcfe, (char *)temp, NULL );
		if(var)
		{
			strncpy((char*)&state.drive_description,var, 512 - 1);
		}

		if(high_res_mode)
			state.sample_rate_hz = 50000000;
		else
			state.sample_rate_hz = 25000000;

		if( prepare_folder( name, comment, start_index, incmode, folder_path) < 0 )
		{
			display_bmp("/data/pauline_splash_bitmaps/error.bmp");

			msg_printf(MSGTYPE_ERROR,"ERROR : Can't create the folder %s !\n",temp);
			dump_running = 0;
			error_sound(fpga);
			return -1;
		}

		display_bmp("/data/pauline_splash_bitmaps/reading_floppy.bmp");

		floppy_ctrl_motor(fpga, drive, 1);

		delay_us( hxcfe_getEnvVarValue(fpga->libhxcfe, "DRIVE_MOTOR_SPINUP_DELAY") * 1000 );

		floppy_ctrl_select_drive(fpga, drive, 1);

		delay_us( 1000 );

		if(dump_start_track!=-1)
		{
			max_track = 160;

			if(hxcfe_getEnvVarValue( fpga->libhxcfe, (char *)"ENABLE_APPLE_MODE" )>0)
			{
				max_track = 80;
			}

			i = 0;
			while( !(floppy_head_at_track00(fpga, drive)) && i < max_track )
			{
				floppy_ctrl_move_head(fpga, 0, 1, drive);
				delay_us( hxcfe_getEnvVarValue(fpga->libhxcfe, "DRIVE_HEAD_STEP_RATE") );
				i++;
			}

			if(i>=160)
			{
				msg_printf(MSGTYPE_ERROR,"Head position calibration failed ! (%d)\n",i);

				floppy_ctrl_select_drive(fpga, drive, 0);
				floppy_ctrl_motor(fpga, drive, 0);
				//floppy_ctrl_selectbyte(fpga, 0x00);

				dump_running = 0;

				display_bmp("/data/pauline_splash_bitmaps/error.bmp");

				error_sound(fpga);

				return -1;
			}

			if(dump_start_track)
			{
				for(i=0;i<dump_start_track;i++)
				{
					floppy_ctrl_move_head(fpga, 1, 1, drive);
				}
			}
		}
		else
		{
			msg_printf(MSGTYPE_INFO_0,"Start spy mode reading...\nTime: %dms, %s\n",dump_time_per_track,high_res_mode?"50Mhz":"25Mhz");

			dump_start_track = fpga->drive_current_head_position[drive];
			if(doublestep)
				dump_start_track /= 2;

			dump_max_track = dump_start_track;
		}
	}
	else
	{
		dump_start_track = fpga->drive_current_head_position[drive];
		if(doublestep)
			dump_start_track /= 2;

		dump_max_track = dump_start_track;
	}

	if( dump_max_track > fpga->drive_max_steps[drive] )
	{
		msg_printf(MSGTYPE_WARNING,"Warning : Drive Max step : %d !\n",fpga->drive_max_steps[drive]);
		dump_max_track = fpga->drive_max_steps[drive];
	}

	// Head load...
	if(fpga->drive_headload_bit_mask[drive])
	{
		usleep(25*1000);

		floppy_ctrl_headload(fpga, drive, 1);

		delay_us( hxcfe_getEnvVarValue(fpga->libhxcfe, "DRIVE_HEAD_LOAD_DELAY") * 1000 );
	}

	for(i=dump_start_track;i<=dump_max_track;i++)
	{
		delay_us( hxcfe_getEnvVarValue(fpga->libhxcfe, "DRIVE_HEAD_SETTLING_TIME") );

		for(j=dump_start_side;j<=dump_max_side;j++)
		{
			if(stop_process)
				goto readstop;

			sprintf(temp,"%s/track%.2d.%d.hxcstream",folder_path,i,j);
			f = fopen(temp,"wb");
			if(!f)
			{
				msg_printf(MSGTYPE_ERROR,"ERROR : Can't create %s\n",temp);

				display_bmp("/data/pauline_splash_bitmaps/error.bmp");

				error_sound(fpga);
			}

			floppy_ctrl_side(fpga, drive, j);

			if(high_res_mode)
				buffersize = (dump_time_per_track * (((50000000 / 16 /*16 bits shift*/ ) * 4 /*A word is 4 bytes*/) / 1000));
			else
				buffersize = (dump_time_per_track * (((25000000 / 16 /*16 bits shift*/ ) * 4 /*A word is 4 bytes*/) / 1000));

			printf_screen(-1, 46, 0x00000000, "T:%.3d H:%d",i,j);

			buffersize += ((0x10 - (buffersize&0xF)) & 0xF);

			fpga->last_dump_offset = 0;
			fpga->bitdelta = 0;
			fpga->chunk_number = 0;

			start_dump(fpga, buffersize, high_res_mode , state.index_to_dump_delay,ignore_index);

			state.current_track = i;
			state.current_side = j;

			while( fpga->last_dump_offset < ( fpga->regs->floppy_dump_buffer_size - 4 ))
			{
				tmpptr = get_next_available_stream_chunk(fpga,&buffersize,&state);
				if(tmpptr)
				{
					if(f)
						fwrite(tmpptr,buffersize,1,f);

					senddatapacket(tmpptr,buffersize);

					free(tmpptr);
				}
				else
				{
					if( fpga->regs->floppy_done & (0x01 << 5) )
					{
						error = PAULINE_NOINDEX_ERROR;
					}
					else
					{
						error = PAULINE_INTERNAL_ERROR;
					}

					i = dump_max_track + 1;
					j = dump_max_side + 1;
					fpga->last_dump_offset = fpga->regs->floppy_dump_buffer_size;
					msg_printf(MSGTYPE_ERROR,"ERROR : get_next_available_stream_chunk failed !\n");
					display_bmp("/data/pauline_splash_bitmaps/error.bmp");
					error_sound(fpga);
				}
			}

			if(f)
			{
				fclose(f);

				if(script_context)
				{
					pthread_mutex_lock(&script_context->script_mutex);
					strcpy(file_to_analyse,temp);
					strcpy(last_file_to_analyse,temp);
					pthread_mutex_unlock(&script_context->script_mutex);
				}
			}

			if(strlen(temp) > 24)
			{
				temp[strlen(temp) - 36] = '.';
				temp[strlen(temp) - 35] = '.';
				temp[strlen(temp) - 34] = '.';
				msg_printf(MSGTYPE_INFO_0,"%s done !\n",&temp[strlen(temp) - 36]);
			}
			else
			{
				msg_printf(MSGTYPE_INFO_0,"%s done !\n",temp);
			}

		}

		if(i<dump_max_track && !spy)
		{
			if(doublestep)
			{
				floppy_ctrl_move_head(fpga, 1, 2, drive);
			}
			else
			{
				floppy_ctrl_move_head(fpga, 1, 1, drive);
			}
		}
	}

readstop:
	free_dump_buffer(fpga);

	dump_running = 0;

	if(!spy)
	{
		//floppy_ctrl_selectbyte(fpga, 0x00);
		if(fpga->drive_headload_bit_mask[drive])
		{
			floppy_ctrl_headload(fpga, drive, 0);

			for(i=0;i<250;i++)
				usleep(1000);
		}

		floppy_ctrl_select_drive(fpga, drive, 0);
		floppy_ctrl_motor(fpga, drive, 0);
	}

	if(error)
	{
		switch(error)
		{
			case PAULINE_INTERNAL_ERROR:
				display_bmp("/data/pauline_splash_bitmaps/internal_error.bmp");
				msg_printf(MSGTYPE_INFO_0,"Internal error !\n");
			break;
			case PAULINE_NOINDEX_ERROR:
				display_bmp("/data/pauline_splash_bitmaps/no_index.bmp");
				msg_printf(MSGTYPE_INFO_0,"No index signal ! Disk in drive ?\n");
			break;
		}

		error_sound(fpga);
	}
	else
	{
		if(!stop_process)
		{
			msg_printf(MSGTYPE_INFO_0,"Done...\n");
			display_bmp("/data/pauline_splash_bitmaps/done.bmp");
		}
		else
		{
			msg_printf(MSGTYPE_INFO_0,"Stopped !!!\n");
			display_bmp("/data/pauline_splash_bitmaps/stopped.bmp");
		}

		sound(fpga, 2000, 200);
		sound(fpga, 0, 200);
		sound(fpga, 2000, 200);
	}


	return 0;
}

void *diskdump_thread(void *threadid)
{
	char * cmdline;
	int p[16];
	char tmp[512];
	char name[512];
	char comment[512];
	char comment2[512];
	char operator[512];
	char driveref[512];

	char str_index_mode[512];
	int i,index_mode;

	cmdline= (char*) threadid;

	pthread_detach(pthread_self());

	for(i=0;i<16;i++)
	{
		if(get_param(cmdline, i + 1,tmp)>=0)
		{
			p[i] = 	atoi(tmp);
		}
	}

	name[0] = 0;
	if(!(get_param(cmdline, 9 + 1,name)>=0))
	{
	}

	if(!strlen(name))
		strcpy(name,"untitled");

	comment[0] = 0;
	if(!(get_param(cmdline, 10 + 1,comment)>=0))
	{
	}

	driveref[0] = 0;
	if(!(get_param(cmdline, 13 + 1,driveref)>=0))
	{
	}

	operator[0] = 0;
	if(!(get_param(cmdline, 14 + 1,operator)>=0))
	{
	}

	comment2[0] = 0;
	if(!(get_param(cmdline, 15 + 1,comment2)>=0))
	{
	}

	if(!(get_param(cmdline, 12 + 1,str_index_mode)>=0))
	{
		str_index_mode[0] = 0;
		index_mode = 2;
	}
	else
	{
		index_mode = 2;

		if(!strcmp(str_index_mode,"AUTO_INDEX_NAME"))
		{
			index_mode = 2;
		}

		if(!strcmp(str_index_mode,"NONE_INDEX_NAME"))
		{
			index_mode = 0;
		}

		if(!strcmp(str_index_mode,"MANUAL_INDEX_NAME"))
		{
			index_mode = 1;
		}
	}

	readdisk(p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],name,comment,comment2,p[11],index_mode,driveref,operator);

	pthread_exit(NULL);
}

static int cmd_dump(script_ctx * ctx, char * line)
{
	int p1,p2,p3,p4,p5,p6,p7,p8,p9,rc;
	char tmpstr[DEFAULT_BUFLEN];

	p1 = get_param(line, 1,tmpstr);
	p2 = get_param(line, 2,tmpstr);
	p3 = get_param(line, 3,tmpstr);
	p4 = get_param(line, 4,tmpstr);
	p5 = get_param(line, 5,tmpstr);
	p6 = get_param(line, 6,tmpstr);
	p7 = get_param(line, 7,tmpstr);
	p8 = get_param(line, 8,tmpstr);
	p9 = get_param(line, 9,tmpstr);

	if(p1>=0 && p2>=0 && p3>=0 && p4>=0 && p5>=0 && p6>=0 && p7>=0 && p8>=0 && p9>=0)
	{
		if(!dump_running)
		{
			strcpy(thread_dump_cmdline,line);

			rc = pthread_create(&threads_dump, NULL, diskdump_thread, (void *)&thread_dump_cmdline);
			if(rc)
			{
				ctx->script_printf(MSGTYPE_ERROR,"Error ! Can't Create the thread ! (Error %d)\r\n",rc);
			}
		}
		else
		{
			ctx->script_printf(MSGTYPE_ERROR,"Error ! Dump already running !\r\n");
		}

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;

}

static int cmd_stop(script_ctx * ctx, char * line)
{
	ctx->script_printf(MSGTYPE_INFO_0,"Stopping current dump...\n");

	stop_process = 1;

	while(dump_running)
	{
		sleep(1);
	}

	stop_process = 0;

	ctx->script_printf(MSGTYPE_INFO_0,"Current dump stopped !\n");

	return 1;
}

static int cmd_print(script_ctx * ctx, char * line)
{
	int i;

	i = get_param_offset(line, 1);
	if(i>=0)
		ctx->script_printf(MSGTYPE_NONE,"%s\n",&line[i]);

	return 1;
}

static int cmd_set_pin_mode(script_ctx * ctx, char * line)
{
	int i,j,k;
	char dev_index[DEFAULT_BUFLEN];
	char pinname[DEFAULT_BUFLEN];
	char mode[DEFAULT_BUFLEN];

	i = get_param(line, 1,dev_index);
	j = get_param(line, 2,pinname);
	k = get_param(line, 3,mode);

	if(i>=0 && j>=0 && k>=0)
	{
		ctx->script_printf(MSGTYPE_INFO_0,"Pin %s mode set to %d\n",pinname,atoi(mode));

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_env(script_ctx * ctx, char * line)
{
	int i,j;
	char variable[DEFAULT_BUFLEN];
	char value[DEFAULT_BUFLEN];

	i = get_param(line, 1,variable);
	j = get_param(line, 2,value);

	if(i>=0 && j>=0)
	{
		hxcfe_setEnvVar( fpga->libhxcfe, variable, value );

		ctx->script_printf(MSGTYPE_INFO_0,"Set %s to %s\n",variable,value);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_get_env(script_ctx * ctx, char * line)
{
	int i;
	char variable[DEFAULT_BUFLEN];
	char value[DEFAULT_BUFLEN];
	char * var;

	i = get_param(line, 1,variable);

	if(i>=0)
	{
		var = hxcfe_getEnvVar( fpga->libhxcfe, variable, NULL );

		ctx->script_printf(MSGTYPE_INFO_0,"%s  = %s\n",variable,var);

		return 1;
	}
	else
	{
		i = 0;
		do
		{
			var = hxcfe_getEnvVarIndex( fpga->libhxcfe, i, value);
			if(var)
			{
				ctx->script_printf(MSGTYPE_INFO_0,"%s  = %s\n",var,value);
			}
			i++;
		}while(var);

		return 1;
	}
}

static int cmd_reload_config(script_ctx * ctx, char * line)
{
	int ret;

	ret = hxcfe_execScriptFile( fpga->libhxcfe, DEFAULT_DRIVES_CFG_FILE );
	if( ret < 0)
	{
		ctx->script_printf(MSGTYPE_ERROR,"Error while reading the default init script !\n");
		return 0;
	}

	reset_fpga(fpga);

	ctx->script_printf(MSGTYPE_INFO_0,"Config file reloaded\n");

	return 1;
}

static int cmd_headstep(script_ctx * ctx, char * line)
{
	int i,j,k,track,dir;
	int drive,doublestep;
	int cur_track;
	char trackstr[DEFAULT_BUFLEN];
	char drivestr[DEFAULT_BUFLEN];
	char doublestepstr[DEFAULT_BUFLEN];

	i = get_param(line, 1,drivestr);
	j = get_param(line, 2,trackstr);
	k = get_param(line, 3,doublestepstr);

	doublestep = 0;

	if(i>=0 && j>=0)
	{
		track = atoi(trackstr);
		drive = atoi(drivestr);

		if(k>=0)
			doublestep = atoi(doublestepstr);

		if( dump_running && (dump_drive != drive))
		{
			ctx->script_printf(MSGTYPE_ERROR,"Floppy bus busy\n");
			return 0;
		}

		ctx->script_printf(MSGTYPE_INFO_0,"Head step : %d\n",track);

		cur_track = fpga->drive_current_head_position[drive];
		if(doublestep)
			cur_track /= 2;

		if( (cur_track >= fpga->drive_max_steps[drive]) && (track>0) )
		{
			ctx->script_printf(MSGTYPE_WARNING,"Warning : Drive Max step : %d !\n",fpga->drive_max_steps[drive]);
			return 0;
		}

		floppy_ctrl_select_drive(fpga, drive, 1);

		usleep(1000);

		if(track < 0)
		{
			track = -track;
			dir = 0;
		}
		else
		{
			dir = 1;
		}

		floppy_ctrl_move_head(fpga, dir, track, drive);
		if(doublestep)
			floppy_ctrl_move_head(fpga, dir, track, drive);

		if(floppy_head_at_track00(fpga, drive))
		{
			fpga->drive_current_head_position[drive] = 0;
		}

		if( !dump_running )
		{
			floppy_ctrl_select_drive(fpga, drive, 0);
		}

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_movehead(script_ctx * ctx, char * line)
{
	int i,j,k,track,dir;
	int cur_track;
	int drive,doublestep;
	char trackstr[DEFAULT_BUFLEN];
	char drivestr[DEFAULT_BUFLEN];
	char doublestepstr[DEFAULT_BUFLEN];

	i = get_param(line, 1,drivestr);
	j = get_param(line, 2,trackstr);
	k = get_param(line, 3,doublestepstr);

	doublestep = 0;

	if(i>=0 && j>=0)
	{
		track = atoi(trackstr);
		drive = atoi(drivestr);
		if(k>=0)
			doublestep = atoi(doublestepstr);

		if( dump_running && (dump_drive != drive))
		{
			ctx->script_printf(MSGTYPE_ERROR,"Floppy bus busy\n");
			return 0;
		}

		cur_track = fpga->drive_current_head_position[drive];
		if(doublestep)
			cur_track /= 2;

		ctx->script_printf(MSGTYPE_INFO_0,"Head move : %d (cur pos: %d)\n",track);

		if( track > fpga->drive_max_steps[drive] )
		{
			ctx->script_printf(MSGTYPE_WARNING,"Warning : Drive Max step : %d !\n",fpga->drive_max_steps[drive]);
			track = fpga->drive_max_steps[drive];
		}

		floppy_ctrl_select_drive(fpga, drive, 1);

		usleep(12000);

		if( cur_track < track )
		{
			track = (track - cur_track);
			dir = 1;
		}
		else
		{
			track = cur_track - track;
			dir = 0;
		}

		if(doublestep)
			track *= 2;

		for(i=0;i<track;i++)
		{
			floppy_ctrl_move_head(fpga, dir, 1, drive);
			usleep(12000);

			if(floppy_head_at_track00(fpga, drive))
			{
				fpga->drive_current_head_position[drive] = 0;
			}
		}

		usleep(12000);

		if( !dump_running )
		{
			floppy_ctrl_select_drive(fpga, drive, 0);
		}
/*
		if(track>0)
		{
			floppy_ctrl_selectbyte(fpga, 0x1F);

			usleep(12000);

			floppy_ctrl_move_head(fpga, dir, track, drive);

			usleep(12000);

			//floppy_ctrl_select_drive(fpga, atoi(drivestr), 0);
			floppy_ctrl_selectbyte(fpga, 0x00);
		}
*/
		sound(fpga, 2000, 50);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_reset(script_ctx * ctx, char * line)
{
	ctx->script_printf(MSGTYPE_INFO_0,"FPGA reset \n");
	reset_fpga(fpga);
	return 0;
}

static int cmd_recalibrate(script_ctx * ctx, char * line)
{
	int i;
	int ret;
	int drive;
	char drivestr[DEFAULT_BUFLEN];

	i = get_param(line, 1,drivestr);
	if(i>=0)
	{
		drive = atoi(drivestr);

		floppy_ctrl_select_drive(fpga, drive, 1);

		usleep(1000);

		ret = floppy_head_recalibrate(fpga, drive);
		if(ret < 0)
		{
			ctx->script_printf(MSGTYPE_ERROR,"Head position calibration failed ! (%d)\n",ret);

			floppy_ctrl_select_drive(fpga, atoi(drivestr), 0);
			return 0;
		}

		if( !dump_running )
		{
			floppy_ctrl_select_drive(fpga, drive, 0);
		}

		sound(fpga, 2000, 50);
	}

	return 1;
}

static int cmd_set_motor_src(script_ctx * ctx, char * line)
{
	int i,j,drive,motsrc;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp);

	if(i>=0 && j>=0)
	{
		drive = atoi(temp);
		motsrc = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d Motor source : %d\n",drive,motsrc);

		set_motor_src(fpga, drive, motsrc);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_select_src(script_ctx * ctx, char * line)
{
	int i,j,drive,selsrc;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp2);

	if(i>=0 && j>=0)
	{
		drive = atoi(temp);
		selsrc = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d select source : %d\n",drive,selsrc);

		set_select_src(fpga, drive, selsrc);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_led_src(script_ctx * ctx, char * line)
{
	int i,j,led,ledsrc;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp2);

	if(i>=0 && j>=0)
	{
		led = atoi(temp);
		ledsrc = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"LED %d signal source : %d\n",led,ledsrc);

		set_led_src(fpga, led, ledsrc);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_pin02_mode(script_ctx * ctx, char * line)
{
	int i,j,drive,mode;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp2);

	if(i>=0 && j>=0)
	{
		drive = atoi(temp);
		mode = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d pin 2 mode : %d\n",drive,mode);

		set_pin02_mode(fpga, drive, mode);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_pin34_mode(script_ctx * ctx, char * line)
{
	int i,j,drive,mode;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp2);

	if(i>=0 && j>=0)
	{
		drive = atoi(temp);
		mode = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d pin 34 mode : %d\n",drive,mode);

		set_pin34_mode(fpga, drive, mode);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_writeprotect(script_ctx * ctx, char * line)
{
	int i,j,drive,writeprotect;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);
	j = get_param(line, 2,temp2);

	if(i>=0 && j>=0)
	{
		drive = atoi(temp);
		writeprotect = atoi(temp2);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d write protect : %d\n",drive,writeprotect);

		floppy_ctrl_writeprotect(fpga, drive, writeprotect);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_ejectdisk(script_ctx * ctx, char * line)
{
	int i;
	int drive;
	char drivestr[DEFAULT_BUFLEN];

	i = get_param(line, 1,drivestr);
	if(i>=0)
	{
		drive = atoi(drivestr);

		floppy_ctrl_x68000_eject(fpga, drive);

		ctx->script_printf(MSGTYPE_INFO_0,"Drive %d : eject disk\n",drive);

		sound(fpga, 2000, 50);

		return 1;
	}

	return 0;
}

static int cmd_set_pin(script_ctx * ctx, char * line)
{
	int i;
	char temp[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);

	if(i>=0)
	{
		ctx->script_printf(MSGTYPE_INFO_0,"set io %s\n",temp);

		setio(fpga, temp, 1);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_clear_pin(script_ctx * ctx, char * line)
{
	int i;
	char temp[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);

	if(i>=0)
	{
		ctx->script_printf(MSGTYPE_INFO_0,"clear io %s\n",temp);

		setio(fpga, temp, 0);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_get_pin(script_ctx * ctx, char * line)
{
	int i,ret;
	char temp[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);

	if(i>=0)
	{
		ret = getio(fpga, temp);
		if(ret >= 0)
		{
			ctx->script_printf(MSGTYPE_INFO_0,"io %s state : %d\n",temp,ret);
			return 1;
		}
		else
		{
			ctx->script_printf(MSGTYPE_ERROR,"Can't get the io %s state ! \n", temp);
			return 0;
		}
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_dump_time_per_track(script_ctx * ctx, char * line)
{
	int i;
	char temp[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);

	if(i>=0)
	{
		dump_time_per_track = atoi(temp);

		if( !dump_time_per_track )
			dump_time_per_track = 800;

		if(dump_time_per_track > 60000)
			dump_time_per_track = 60000;

		ctx->script_printf(MSGTYPE_INFO_0,"dump_time_per_track : %d\n",dump_time_per_track);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_index_to_dump_time(script_ctx * ctx, char * line)
{
	int i;
	char temp[DEFAULT_BUFLEN];

	i = get_param(line, 1,temp);

	if(i>=0)
	{
		index_to_dump_delay = atoi(temp);

		ctx->script_printf(MSGTYPE_INFO_0,"index_to_dump_delay : %d\n",index_to_dump_delay);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static int cmd_set_images_settings(script_ctx * ctx, char * line)
{
	int i,int_val[5];
	char temp[DEFAULT_BUFLEN];

	for(i=0;i<5;i++)
	{
		int_val[i] = 0;
		if(get_param(line, i + 1,temp) >= 0)
		{
			int_val[i] = atoi(temp);
		}
	}

	pthread_mutex_lock(&ctx->script_mutex);

	preview_image_xtime = int_val[0];
	preview_image_ytime = int_val[1];
	preview_image_xoffset = int_val[2];

	if(int_val[3])
		preview_image_flags |= TD_FLAG_HICONTRAST;
	else
		preview_image_flags &= (~TD_FLAG_HICONTRAST);

	if(int_val[4])
		preview_image_flags |= TD_FLAG_BIGDOT;
	else
		preview_image_flags &= (~TD_FLAG_BIGDOT);

	strcpy(file_to_analyse,last_file_to_analyse);

	pthread_mutex_unlock(&ctx->script_mutex);

	ctx->script_printf(MSGTYPE_INFO_0,"cmd_set_images_settings : %d %d %d %d %d\n",int_val[0],int_val[1],int_val[2],int_val[3],int_val[4]);

	return 1;
}

static int cmd_set_images_decoders(script_ctx * ctx, char * line)
{
	pthread_mutex_lock(&ctx->script_mutex);

	if(strstr(line,"ISOMFM"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ISOIBM_MFM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ISOIBM_MFM_ENCODING", "0" );

	if(strstr(line,"ISOFM"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ISOIBM_FM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ISOIBM_FM_ENCODING", "0" );

	if(strstr(line,"AMIGAMFM"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_AMIGA_MFM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_AMIGA_MFM_ENCODING", "0" );

	if(strstr(line,"APPLE"))
	{
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEII_GCR1_ENCODING", "1" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEII_GCR2_ENCODING", "1" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEMAC_GCR_ENCODING", "1" );
	}
	else
	{
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEII_GCR1_ENCODING", "0" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEII_GCR2_ENCODING", "0" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_APPLEMAC_GCR_ENCODING", "0" );
	}

	if(strstr(line,"EEMU"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_EMU_FM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_EMU_FM_ENCODING", "0" );

	if(strstr(line,"TYCOM"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_TYCOM_FM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_TYCOM_FM_ENCODING", "0" );

	if(strstr(line,"MEMBRAIN"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_MEMBRAIN_MFM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_MEMBRAIN_MFM_ENCODING", "0" );

	if(strstr(line,"ARBURG"))
	{
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ARBURGDAT_ENCODING", "1" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ARBURGSYS_ENCODING", "1" );
	}
	else
	{
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ARBURGDAT_ENCODING", "0" );
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_ARBURGSYS_ENCODING", "0" );
	}
/*
	if(strstr(line,"AED6200P"))
		hxcfe_setEnvVar( fpga->libhxcfe, "", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "", "0" );
*/

	if(strstr(line,"NORTHSTAR"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_NORTHSTAR_HS_MFM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_NORTHSTAR_HS_MFM_ENCODING", "0" );

	if(strstr(line,"HEATHKIT"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_HEATHKIT_HS_FM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_HEATHKIT_HS_FM_ENCODING", "0" );

	if(strstr(line,"DECRX02"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_DEC_RX02_M2FM_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_DEC_RX02_M2FM_ENCODING", "0" );

	if(strstr(line,"C64"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_C64_GCR_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_C64_GCR_ENCODING", "0" );

	if(strstr(line,"VICTOR9K"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_VICTOR9000_GCR_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_VICTOR9000_GCR_ENCODING", "0" );

	if(strstr(line,"QD_MO5"))
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_QD_MO5_ENCODING", "1" );
	else
		hxcfe_setEnvVar( fpga->libhxcfe, "BMPEXPORT_ENABLE_QD_MO5_ENCODING", "0" );

	strcpy(file_to_analyse,last_file_to_analyse);

	pthread_mutex_unlock(&ctx->script_mutex);

	ctx->script_printf(MSGTYPE_INFO_0,"cmd_set_images_settings : %s\n",line);

	return 1;
}

static int cmd_help(script_ctx * ctx, char * line);

static int cmd_version(script_ctx * ctx, char * line)
{
	ctx->script_printf(MSGTYPE_INFO_0,"HxC Streamer version : %s, Date : "STR_DATE", Build Date : "__DATE__" "__TIME__"\n",STR_FILE_VERSION2);
	return 1;
}

static int cmd_system(script_ctx * ctx, char * line)
{
	int i,ret;
	char temp[DEFAULT_BUFLEN];
	char temp2[DEFAULT_BUFLEN];

	memset(temp2,0,sizeof(temp2));

	for(i=1;i<32;i++)
	{
		temp[0] = 0;
		get_param(line, i,temp);
		if(strlen(temp))
		{
			strcat(temp2," ");
			strcat(temp2,temp);
		}
	}

	ret = system(temp2);

	ctx->script_printf(MSGTYPE_ERROR,"system() return : %d\n",ret);

	return 0;
}

static int cmd_sound(script_ctx * ctx, char * line)
{
	int i,j;
	int freq,duration;
	char durationstr[DEFAULT_BUFLEN];
	char freqstr[DEFAULT_BUFLEN];

	i = get_param(line, 1,freqstr);
	j = get_param(line, 2,durationstr);

	if(i>=0 && j>=0)
	{
		freq = atoi(freqstr);
		duration = atoi(durationstr);

		sound(fpga, freq, duration);

		return 1;
	}

	ctx->script_printf(MSGTYPE_ERROR,"Bad/Missing parameter(s) ! : %s\n",line);

	return 0;
}

static cmd_list cmdlist[] =
{
	{"print",                   cmd_print},
	{"help",                    cmd_help},
	{"?",                       cmd_help},
	{"version",                 cmd_version},

	{"set_pin_dir",             cmd_set_pin_mode},
	{"headstep",                cmd_headstep},
	{"dump_time",               cmd_set_dump_time_per_track},
	{"index_to_dump",           cmd_set_index_to_dump_time},
	{"reset",                   cmd_reset},
	{"recalibrate",             cmd_recalibrate},
	{"dump",                    cmd_dump},
	{"stop",                    cmd_stop},
	{"movehead",                cmd_movehead},
	{"ejectdisk",               cmd_ejectdisk},

	{"setio",                   cmd_set_pin},
	{"cleario",                 cmd_clear_pin},
	{"getio",                   cmd_get_pin},

	{"setpreviewimagesettings", cmd_set_images_settings},
	{"setpreviewimagedecoders", cmd_set_images_decoders},

	{"set",                     cmd_set_env},
	{"get",                     cmd_get_env},
	{"reloadcfg",               cmd_reload_config},

	{"ledsrc",                  cmd_set_led_src},

	{"sound",                   cmd_sound},

	{"system",                  cmd_system},

	{"fe_writeprotect",         cmd_set_writeprotect},
	{"fe_motsrc",               cmd_set_motor_src},
	{"fe_selsrc",               cmd_set_select_src},
	{"fe_pin02mode",            cmd_set_pin02_mode},
	{"fe_pin34mode",            cmd_set_pin34_mode},

	{0 , 0}
};


static int extract_cmd(char * line, char * command)
{
	int offs,i;

	i = 0;
	offs = 0;

	offs = get_next_word(line, offs);

	if( !is_end_line(line[offs]) )
	{
		while( !is_end_line(line[offs]) && !is_space(line[offs]) && i < (DEFAULT_BUFLEN - 1) )
		{
			command[i] = line[offs];
			offs++;
			i++;
		}

		command[i] = 0;

		return i;
	}

	return 0;
}

static int exec_cmd(script_ctx * ctx, char * command,char * line)
{
	int i;

	i = 0;
	while(cmdlist[i].func)
	{
		if( !strcmp(cmdlist[i].command,command) )
		{
			cmdlist[i].func(ctx, line);
			return 1;
		}

		i++;
	}

	return ERROR_CMD_NOT_FOUND;
}

static int cmd_help(script_ctx * ctx, char * line)
{
	int i;

	ctx->script_printf(MSGTYPE_INFO_0,"Supported Commands :\n\n");

	i = 0;
	while(cmdlist[i].func)
	{
		ctx->script_printf(MSGTYPE_NONE,"%s\n",cmdlist[i].command);
		i++;
	}

	return 1;
}

int pauline_execute_line(script_ctx * ctx, char * line)
{
	char command[DEFAULT_BUFLEN];

	if( extract_cmd(line, command) )
	{
		if(strlen(command))
		{
			if(exec_cmd(ctx, command,line) == ERROR_CMD_NOT_FOUND )
			{
				ctx->script_printf(MSGTYPE_ERROR,"Command not found ! : %s\n",line);
				return 0;
			}
		}
		return 1;
	}

	return 0;
}

int pauline_execute_script(script_ctx * ctx, char * filename)
{
	FILE * f;
	char script_line[MAX_LINE_SIZE];

	f = fopen(filename,"r");
	if(f)
	{
		do
		{
			if(fgets(script_line,sizeof(script_line)-1,f))
			{
				pauline_execute_line(ctx, script_line);
			}
		}while(!feof(f));
		fclose(f);
	}
	return 0;
}
