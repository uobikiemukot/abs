/*
** Portions Copyright (C) 2003 Daniel Caujolle-Bert <segfault@club-internet.fr>
** Portions Copyright (C) 2004 Rett D. Walters <rettw@rtwnetwork.com>
** Portions Copyright (C) 2006, 2007 Anders Rune Jense <anders@gnulinux.dk>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
*/
#include "config.h"
#include "main.h"
#include "post.h"
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "termio/getch2.h"
#include "termio/keycodes.h"

#ifdef HAVE_LIBLIRC_CLIENT
#include "lirc/lirc_client.h"
#endif

#define INPUT_MOTION (ExposureMask | ButtonPressMask | KeyPressMask | ButtonMotionMask |\
			StructureNotifyMask | PropertyChangeMask | PointerMotionMask)
#define DEFAULT_DEINTERLACER "tvtime:method=LinearBlend,cheap_mode=1,pulldown=0,use_progressive_frame_flag=1"
 
#define DEFAULT_WIDTH 960
#define DEFAULT_HEIGHT 540

#define SAFE_FREE(x)            do {           \
                                  if((x)) {    \
                                    free((x)); \
                                    x = NULL;  \
                                  }            \
                                } while(0)

#ifndef XShmGetEventBase
extern int XShmGetEventBase(Display *);
#endif

static xine_osd_t	   *osd;

static xine_event_queue_t  *event_queue;
static Display             *display;
static int                  screen;
static Window               window[2];
static int                  completion_event;
static int                  xpos, ypos, width, height, fullscreen;
static double               pixel_aspect;

static int                  running = 0;
static int		    next_mrl = FALSE;
static int		    volume;
static int		    vo_hue, vo_brightness, vo_saturation, vo_contrast;
static int		    lirc = 0;
static int		    aspect = 0;
static int		    osd_timeout = 2;
static int		    osd_info_visible = 0;
static int		    fd;
char                      **pplugins        = NULL;
int                         pplugins_num    = 0;

struct lirc_config 	   *config;
struct cxfe cxfe = 
{
	post_elements: NULL,
        post_elements_num: 0,
        post_enable: 1,

        deinterlace_plugin: NULL,
        deinterlace_elements: NULL,
        deinterlace_elements_num: 0,
        deinterlace_enable: 0,
	deinterlace_capable: 1,
	custom_deint_plugin: 0
};

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5

typedef struct {
  uint32_t  flags;
  uint32_t  functions;
  uint32_t  decorations;
  int32_t   input_mode;
  uint32_t  status;
} MWMHints;

static int dpms_disabled=0;
static int timeout_save=0;

void wsScreenSaverOn( Display *mDisplay )
{
 int nothing;
 if ( dpms_disabled )
  {
   if ( DPMSQueryExtension( mDisplay,&nothing,&nothing ) )
    {
     if ( !DPMSEnable( mDisplay ) ) printf("DPMS restoring failed.\n"); // restoring power saving settings
      else
       {
        // DPMS does not seem to be enabled unless we call DPMSInfo
        BOOL onoff;
        CARD16 state;
        DPMSInfo( mDisplay,&state,&onoff );
        if ( onoff ) printf("Successfully enabled DPMS.\n");
         else printf("DPMS not enabled.\n");
       }
    }
  }
 if ( timeout_save )
  {
   int dummy, interval, prefer_blank, allow_exp;
   XGetScreenSaver( mDisplay,&dummy,&interval,&prefer_blank,&allow_exp );
   XSetScreenSaver( mDisplay,timeout_save,interval,prefer_blank,allow_exp );
   XGetScreenSaver( mDisplay,&timeout_save,&interval,&prefer_blank,&allow_exp );
  }
}

void wsScreenSaverOff( Display * mDisplay )
{
 int interval,prefer_blank,allow_exp,nothing;
 if ( DPMSQueryExtension( mDisplay,&nothing,&nothing ) )
  {
   BOOL onoff;
   CARD16 state;
   DPMSInfo( mDisplay,&state,&onoff );
   if ( onoff )
    {
      Status stat;
      printf("Disabling DPMS.\n");
      dpms_disabled=1;
      stat=DPMSDisable( mDisplay );  // monitor powersave off
      //mp_dbg( MSGT_GPLAYER,MSGL_DBG2,"stat: %d.\n",stat );
   }
  }
 XGetScreenSaver( mDisplay,&timeout_save,&interval,&prefer_blank,&allow_exp );
 if ( timeout_save ) XSetScreenSaver( mDisplay,0,interval,prefer_blank,allow_exp ); // turning off screensaver
}

static void *osd_loop(void *dummy)
{
        pthread_detach(pthread_self());
        while(1) {
            sleep(1);
            if(osd_info_visible) {
               osd_info_visible--;
              if(!osd_info_visible) {
                xine_osd_hide(osd, 0);
              }
            }
        }
}

void osd_display_info(char *info, ...) {

//  if(fbxine.osd.enabled) {
    va_list   args;
    char     *buf;
    int       n, size = 47;
    
    if((buf = xine_xmalloc(size)) == NULL) 
      return;
    
    va_start(args, info);
    n = vsnprintf(buf, size, info, args);
    va_end(args);

    buf = realloc(buf, size+3);
    buf[size-1] = '.';
    buf[size] = '.';
    buf[size+1] = '.';
    buf[size+2] = '\0';

#if 0
    while(1) {
      
      va_start(args, info);
      n = vsnprintf(buf, size, info, args);
      va_end(args);
      
      if(n > -1 && n < size)
	break;
      
      if(n > -1)
	size = n + 1;
      else
	size *= 2;
      
      if((buf = realloc(buf, size)) == NULL)
	return;
    }
#endif

    xine_osd_clear(osd);

    xine_osd_draw_text(osd, 0, 0, buf, XINE_OSD_TEXT1);
    xine_osd_set_position(osd, 20, 10 + 30);
    xine_osd_show(osd, 0);
    osd_info_visible = osd_timeout;
    SAFE_FREE(buf);
}

static int play(xine_stream_t *stream, 
		int start_pos, int start_time_in_secs, int update_mmk)
{
	return xine_play(stream, start_pos, start_time_in_secs * 1000);
}

int get_pos_length(xine_stream_t *stream, int *pos, int *time, int *length)
{
	int t = 0, ret = 0;

	if(stream && xine_get_status(stream) == XINE_STATUS_PLAY)
		for(;;)
		{
			ret = xine_get_pos_length(stream, pos, time, length);
			if(ret || 10 < ++t)
				break;
			xine_usec_sleep(100000); /* wait before trying again */
		}
	
	return ret;
}

static void *seek_relative_thread(void *data)
{
	int sec, off_sec = (int)data;
	
	pthread_detach(pthread_self());
	
	if(get_pos_length(cxfe.stream, 0, &sec, 0))
	{
		sec /= 1000;
		
		if(sec + off_sec < 0)
			sec = 0;
		else
			sec += off_sec;
		
		play(cxfe.stream, 0, sec, 1);
                //osd_stream_position();
	}
	
	//ignore_next = 0;
	pthread_exit(0);
	return 0;
}
static void action_seek_relative(int off_sec)
{
	static pthread_t seek_thread;
	int err;
          if (!xine_get_stream_info(cxfe.stream, XINE_STREAM_INFO_SEEKABLE) ||
	   xine_get_status(cxfe.stream) != XINE_STATUS_PLAY)
		return;
    
	//fbxine.ignore_next = 1;

	err = pthread_create(&seek_thread, 0, seek_relative_thread,
			     (void *)off_sec);
	if(!err){
	        osd_display_info("   Seek:%d Seconds",off_sec);
		return;
		}

	printf("Failed to create action_seek_relative thread.\n");
	abort();
}
static void toggle_deinterlace()
{
  if(cxfe.deinterlace_capable){
     cxfe.deinterlace_enable = !cxfe.deinterlace_enable;
     osd_display_info("    Deinterlace: %s", (cxfe.deinterlace_enable) ? "enabled" : "disabled");
     post_deinterlace();
  }
}

static void aspect_change()
{ 

  static char *ratios[XINE_VO_ASPECT_NUM_RATIOS + 1] = {
    "    Auto",
    "    Square",
    "    4:3",
    "    Anamorphic",
    "    DVB",
    NULL
  };
   
  aspect = xine_get_param(cxfe.stream, XINE_PARAM_VO_ASPECT_RATIO);
  printf("Aspect:%d\n",aspect);
  if(aspect == 4)
    aspect = -1;
  
  xine_set_param(cxfe.stream, XINE_PARAM_VO_ASPECT_RATIO, aspect+1);
  osd_display_info(ratios[xine_get_param(cxfe.stream, XINE_PARAM_VO_ASPECT_RATIO)]);
}
void change_zoom(int zoom_dx, int zoom_dy) {

  xine_set_param(cxfe.stream, XINE_PARAM_VO_ZOOM_X,
		 xine_get_param(cxfe.stream, XINE_PARAM_VO_ZOOM_X) + zoom_dx);
  xine_set_param(cxfe.stream, XINE_PARAM_VO_ZOOM_Y,
		 xine_get_param(cxfe.stream, XINE_PARAM_VO_ZOOM_Y) + zoom_dy);
}
static void action_volume_up()
{
	if (volume < 100) {
        volume ++;
        xine_set_param(cxfe.stream, XINE_PARAM_AUDIO_VOLUME,volume);
	}
	osd_display_info("    Volume: %d",volume);
        printf("Volume: %d\n",volume);
}
static void action_volume_down()
{
	if (volume > 0) {
        volume --;
        xine_set_param(cxfe.stream, XINE_PARAM_AUDIO_VOLUME,volume);
        }
	osd_display_info("    Volume: %d",volume);
        printf("Volume: %d\n",volume);
}
static void action_pause()
{
	if(xine_get_param(cxfe.stream, XINE_PARAM_SPEED) != XINE_SPEED_PAUSE){
	       xine_set_param(cxfe.stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);
	       osd_display_info("   Pause");
	       }
	else {
	       xine_set_param(cxfe.stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);
	       osd_display_info("   Play");
	     }
}

static void dest_size_cb(void *data, int video_width, int video_height, double video_pixel_aspect,
			 int *dest_width, int *dest_height, double *dest_pixel_aspect)  {


  if(!running)
     return;

  *dest_width        = width;
  *dest_height       = height;
  *dest_pixel_aspect = pixel_aspect;
}

static void frame_output_cb(void *data, int video_width, int video_height,
			    double video_pixel_aspect, int *dest_x, int *dest_y,
			    int *dest_width, int *dest_height,
			    double *dest_pixel_aspect, int *win_x, int *win_y) {

  //printf("frame_output_cb\n");
  *dest_x            = 0;
  *dest_y            = 0;
  *win_x             = xpos;
  *win_y             = ypos;
  *dest_width        = width;
  *dest_height       = height;
  *dest_pixel_aspect = video_pixel_aspect;
}

//static void frame_output_cb_x11(void *data, int video_width, int video_height,
//				double video_pixel_aspect, int *dest_x, int *dest_y,
//				int *dest_width, int *dest_height,
//				double *dest_pixel_aspect, int *win_x, int *win_y) {
//  if(!running)
//    return;

  // resize the window when a new movie starts
//  if (video_width != movie_height && video_width != movie_width && !fullscreen) {
//    width = video_width;
//    height = video_height;
//    movie_width = video_width;
//   movie_height = video_height;

//    XResizeWindow(display, window[fullscreen], width, height);
//  }

//  *dest_x            = 0;
//  *dest_y            = 0;
//  *win_x             = xpos;
//  *win_y             = ypos;
//  *dest_width        = width;
//  *dest_height       = height;
//  *dest_pixel_aspect = pixel_aspect;
//}

// Functions for VO controls (brightness, hue, color, Contrast, etc)

static int get_current_param(int param) 
{
  return (xine_get_param(cxfe.stream, param));
}

/*
 * set parameter 'param' to value 'value'.
 */
static void set_current_param(int param, int value)
{
  xine_set_param(cxfe.stream, param, value);
}

static void increase_vo_contrast()
{
  if (vo_contrast < 64535){
     vo_contrast = vo_contrast + 1000;
     set_current_param(XINE_PARAM_VO_CONTRAST, vo_contrast);
     }
     printf("Contrast:%d\n",vo_contrast);
     osd_display_info("    Contrast:%d\n",vo_contrast);
}

static void decrease_vo_contrast()
{
  if (vo_contrast > 1000){
     vo_contrast = vo_contrast - 1000;
     set_current_param(XINE_PARAM_VO_CONTRAST, vo_contrast);
     printf("Contrast:%d\n",vo_contrast);
     }
     printf("Contrast:%d\n",vo_contrast);
     osd_display_info("    Contrast:%d\n",vo_contrast);
}

static void increase_vo_brightness()
{
  if (vo_brightness < 64535){
     vo_brightness = vo_brightness + 1000;
     set_current_param(XINE_PARAM_VO_BRIGHTNESS, vo_brightness);
     }
     printf("Brightness:%d\n",vo_brightness);
     osd_display_info("    Brightness:%d\n",vo_brightness);
}

static void decrease_vo_brightness()
{
  if (vo_brightness > 1000){
     vo_brightness = vo_brightness - 1000;
     set_current_param(XINE_PARAM_VO_BRIGHTNESS, vo_brightness);
     printf("Brightness:%d\n",vo_brightness);
     }
     printf("Brightness:%d\n",vo_brightness);
     osd_display_info("    Brightness:%d\n",vo_brightness);
}
static void increase_vo_saturation()
{
  if (vo_saturation < 64535){
     vo_saturation = vo_saturation + 1000;
     set_current_param(XINE_PARAM_VO_SATURATION, vo_saturation);
     }
     printf("Saturation:%d\n",vo_saturation);
     osd_display_info("    Saturation:%d\n",vo_saturation);
}

static void decrease_vo_saturation()
{
  if (vo_saturation > 1000){
     vo_saturation = vo_saturation - 1000;
     set_current_param(XINE_PARAM_VO_SATURATION, vo_saturation);
     }
     printf("Saturation:%d\n",vo_saturation);
     osd_display_info("    Saturation:%d\n",vo_saturation);
}
static void increase_vo_hue()
{
  if (vo_hue < 64535){
     vo_hue = vo_hue + 1000;
     set_current_param(XINE_PARAM_VO_HUE, vo_hue);
     }
     printf("Hue:%d\n",vo_hue);
     osd_display_info("    Hue:%d\n",vo_hue);
}
static void decrease_vo_hue()
{
  if (vo_hue > 1000){
     vo_hue = vo_hue - 1000;
     set_current_param(XINE_PARAM_VO_HUE, vo_hue);
     }
     printf("Hue:%d\n",vo_hue);
     osd_display_info("    Hue:%d\n",vo_hue);
}
static void send_event(int event)
{
   xine_event_t xine_event;
   		
                xine_event.type = event;
		xine_event.data_length = 0;
		xine_event.data = 0;
		xine_event.stream = cxfe.stream;
		gettimeofday(&xine_event.tv, NULL);
		
		xine_event_send(cxfe.stream, &xine_event);
		return;
}

static void event_listener(void *user_data, const xine_event_t *event) {
  
  switch(event->type) {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
    running = 0;
    printf("Xine Finished Event.\n");
    xine_stop(cxfe.stream);
    break;

  case XINE_EVENT_PROGRESS:
    {
      xine_progress_data_t *pevent = (xine_progress_data_t *) event->data;

      printf("%s [%d%%]\n", pevent->description, pevent->percent);
    }
    break;

  }
}
static void print_status()
{
   int               lpos, ltime, llength;
   float	     percent_done;
   
   
   (void)xine_get_pos_length(cxfe.stream, &lpos, &ltime, &llength);
   percent_done = (ltime/llength)*100;
   printf("Time:%d sec Length:%d sec \n",ltime/1000,llength/1000);
}
static void print_osd_status()
{
   int	     lpos, ltime, llength;
   int	     percent_done;
   
   
   (void)get_pos_length(cxfe.stream, &lpos, &ltime, &llength);

   percent_done = (int)(((double)ltime/(double)llength)*100);

   int cur_hour = ltime/1000/60/60;
   int cur_min = (ltime/1000-cur_hour*60*60)/60;
   int cur_sec= ltime/1000-cur_hour*60*60-cur_min*60;

   int tot_hour = llength/1000/60/60;
   int tot_min = (llength/1000-tot_hour*60*60)/60;
   int tot_sec= llength/1000-tot_hour*60*60-tot_min*60;
   
   osd_display_info("%02d:%02d:%02d / %02d:%02d:%02d (%2d%%)", cur_hour, 
		    cur_min, cur_sec, tot_hour, tot_min, tot_sec, 
		    percent_done);
}
#ifdef HAVE_LIBLIRC_CLIENT
static void *process_lirc_thread()
{
     pthread_detach(pthread_self());
     
     while(1){
     if(lirc_nextcode(&code)==0)
     {
        if(code!=NULL)
        while((lirc_ret=lirc_code2char(config,code,&c))==0 && c!=NULL)
        {
                printf("LIRC command \"%s\"\n",c);
                //Process LIRC input
                if (!strcmp(c,"PREV_EVENT"))
                    send_event(XINE_EVENT_INPUT_PREVIOUS);
                if (!strcmp(c,"NEXT_EVENT"))
                    send_event(XINE_EVENT_INPUT_NEXT);
                if (!strcmp(c,"UP_EVENT"))
                    send_event(XINE_EVENT_INPUT_UP);
                if (!strcmp(c,"DOWN_EVENT"))
                    send_event(XINE_EVENT_INPUT_DOWN);
                if (!strcmp(c,"LEFT_EVENT"))
                    send_event(XINE_EVENT_INPUT_LEFT);
                if (!strcmp(c,"RIGHT_EVENT"))
                    send_event(XINE_EVENT_INPUT_RIGHT);
                if (!strcmp(c,"SELECT_EVENT"))
                    send_event(XINE_EVENT_INPUT_SELECT);
		if (!strcmp(c,"FF"))
		    action_seek_relative(60);
		if (!strcmp(c,"REW"))
		    action_seek_relative(-15);
		if (!strcmp(c,"VOL_UP"))
		    action_volume_up();
		if (!strcmp(c,"VOL_DOWN"))
		    action_volume_down();
                if (!strcmp(c,"Pause"))
                    action_pause();
                if (!strcmp(c,"Stop"))
                    running = 0;
                if (!strcmp(c,"Quit")){
                    running = 0;
                    next_mrl = FALSE;}
		if (!strcmp(c,"STATUS"))
		    print_osd_status();
		if (!strcmp(c,"ASPECT_CHANGE"))
		    aspect_change();
		if (!strcmp(c,"Menu"))
		    send_event(XINE_EVENT_INPUT_MENU1);
		if (!strcmp(c,"Title"))
		    send_event(XINE_EVENT_INPUT_MENU3);
		if (!strcmp(c,"Language"))
		    send_event(XINE_EVENT_INPUT_MENU2);
		if (!strcmp(c,"Angle"))
		    send_event(XINE_EVENT_INPUT_MENU4);
		if (!strcmp(c,"Subtitles"))
		    send_event(XINE_EVENT_INPUT_MENU5);        
        }
        free(code);
     }
   }
}
#endif
static void config_update(xine_cfg_entry_t *entry, int type, int min, int max, int value, char *string) {

  switch(type) {

  case XINE_CONFIG_TYPE_UNKNOWN:
    printf("Config key '%s' isn't registered yet.\n", entry->key);
    return;
    break;

  case XINE_CONFIG_TYPE_RANGE:
    entry->range_min = min;
    entry->range_max = max;
    break;

  case XINE_CONFIG_TYPE_STRING:
    entry->str_value = string;
    break;

  case XINE_CONFIG_TYPE_ENUM:
  case XINE_CONFIG_TYPE_NUM:
  case XINE_CONFIG_TYPE_BOOL:
    entry->num_value = value;
    break;

  default:
    printf("Unknown config type %d\n", type);
    return;
    break;
  }

  xine_config_update_entry(cxfe.xine, entry);
}

void config_update_string(char *key, char *string) {
  xine_cfg_entry_t entry;

  if((xine_config_lookup_entry(cxfe.xine, key, &entry)) && string)
    config_update(&entry, XINE_CONFIG_TYPE_STRING, 0, 0, 0, string);
  else {
    if(string == NULL)
      printf("string is NULL\n");
    else
      printf("WOW, string key %s isn't registered\n", key);
  }
}

static void cxfe_run_console(){

   int               ch;

   
   getch2_enable();

   while(running) {

     ch = getch2(1);
	  if (ch>0)
	     printf("KeyPress = %d\n",ch);
        switch (ch) {
	case 'q': // Quit (q)
		running = 0;
		next_mrl = FALSE;
		printf("Quiting at user request.\n");
		break;
	case 272: // Seek forward ~60 secs (Right Arrow)
		action_seek_relative(60);
		break;

	case ' ': // Pause (Space Bar)
		action_pause();
		break;
	case 273: // Seek back ~15 secs (Left Arrow)
		action_seek_relative(-15);
		break;
	case 275: //Increase Volume
                action_volume_up();
	        break;
	case 274: //Decrease Volume
	        action_volume_down();
	        break;
	case 49:
                increase_vo_contrast();
                break;
        case 50:
                decrease_vo_contrast();
                break;
	case 51:
                increase_vo_brightness();
                break;
        case 52:
                decrease_vo_brightness();
                break;
        case 53:
                increase_vo_saturation();
                break;
        case 54:
                decrease_vo_saturation();
                break; 
        case 55:
                increase_vo_hue();
                break;
        case 56:
                decrease_vo_hue();
                break;
        case 262:
        	send_event(XINE_EVENT_INPUT_NEXT);
                break;
        case 261:
        	send_event(XINE_EVENT_INPUT_PREVIOUS);
        	break;
	case 'a':
	case 'A':
	        aspect_change();
		break;
        case 's':
		running = 0;
		break;
	case 'i':
        case 'I':
           	send_event(XINE_EVENT_INPUT_UP);
           	break;
        case 'k':
        case 'K':
           	send_event(XINE_EVENT_INPUT_RIGHT);
           	break;
        case 'j':
        case 'J':
           	send_event(XINE_EVENT_INPUT_LEFT);
           	break;
        case 'm':
        case 'M':
           	send_event(XINE_EVENT_INPUT_DOWN);
           	break;
	case 'z':
	        change_zoom(1,1);
		break;
	case 'Z':
		change_zoom(-1,-1);
		break;
        case 'd':
	case 'D':
	        toggle_deinterlace();
		break;
	case 'o':
	case 'O':
	        print_osd_status();
		break;
        case 'l':
        case 13:
           	send_event(XINE_EVENT_INPUT_SELECT);
           	break;
        case 321:
           	send_event(XINE_EVENT_INPUT_MENU1);
           	break;
        case 322:
           	send_event(XINE_EVENT_INPUT_MENU2);
           	break;
        case 323:
           	send_event(XINE_EVENT_INPUT_MENU3);
           	break;
        case 324:
        	send_event(XINE_EVENT_INPUT_MENU4);
                break;
        case 325:
        	send_event(XINE_EVENT_INPUT_MENU5);
                break;
        }
     }
   getch2_disable();
 }

static void cxfe_run_x11() {

   
   while(running) {

       XEvent   xevent;
       int got_event;

       
       xine_usec_sleep(10000);
       
       XLockDisplay(display);
       got_event = XCheckMaskEvent(display, INPUT_MOTION, &xevent);
       XUnlockDisplay(display);
       
       if(!got_event)
         continue;

       //printf("Event Switch Code\n");
       
       switch(xevent.type) {
       

       case KeyPress:
         {
	   XKeyEvent  kevent;
	   KeySym     ksym;
	   char       kbuf[256];
	   int        len;

	   kevent = xevent.xkey;

	   XLockDisplay(display);
	   len = XLookupString(&kevent, kbuf, sizeof(kbuf), &ksym, NULL);
	   XUnlockDisplay(display);

	   switch (ksym) {

	   case XK_q:
	   case XK_Q:
	     running = 0;
	     next_mrl = FALSE;
	     break;

	   case XK_f:
	   case XK_F:
	     {
	       Window    tmp_win;

	       XLockDisplay(display);
	       XUnmapWindow(display, window[fullscreen]);
	       fullscreen = !fullscreen;
	       if (fullscreen)
		 wsScreenSaverOff(display);
	       else
		 wsScreenSaverOn(display);
	       XMapRaised(display, window[fullscreen]);
	       XSync(display, False);
	       XTranslateCoordinates(display, window[fullscreen],
				     DefaultRootWindow(display),
				     0, 0, &xpos, &ypos, &tmp_win);
	       XUnlockDisplay(display);

	       xine_gui_send_vo_data(cxfe.stream, XINE_GUI_SEND_DRAWABLE_CHANGED,
		  		     (void*) window[fullscreen]);
	     }
	     
	     break;
		
	   case XK_Up:
	     action_volume_up();
	     break;

	   case XK_Down:
	     action_volume_down();
	     break;

	   case XK_Right:
                action_seek_relative(60);
		break;

           case XK_Left:
                action_seek_relative(-15);
		break;

	   case XK_plus:
	     xine_set_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL,
		   	    (xine_get_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL) + 1));
	     osd_display_info("Audio Channel:%d",xine_get_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL));
	     break;

	   case XK_minus:
	      xine_set_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL,
			    (xine_get_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL) - 1));
	      osd_display_info("Audio Channel:%d",xine_get_param(cxfe.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL));	    
	     break;
           case XK_1:
              increase_vo_contrast();
              break;
           case XK_2:
              decrease_vo_contrast();
              break;
	   case XK_3:
              increase_vo_brightness();
              break;
           case XK_4:
              decrease_vo_brightness();
              break;
           case XK_5:
              increase_vo_saturation();
              break;
           case XK_6:
              decrease_vo_saturation();
              break; 
           case XK_7:
              increase_vo_hue();
              break;
           case XK_8:
              decrease_vo_hue();
              break;  
           case XK_space:
	     action_pause();
             break;
           case XK_d:
	   case XK_D:
	     toggle_deinterlace();
	     break;
	   case XK_Page_Down:
             send_event(XINE_EVENT_INPUT_NEXT);
             break;
           case XK_Page_Up:
             send_event(XINE_EVENT_INPUT_PREVIOUS);
             break;
           case XK_a:
	   case XK_A:
	     aspect_change();
	     break;
	   case XK_s:
	   case XK_S:
	     running = 0;
	     break;
	   case XK_z:
	     change_zoom(1,1);
	     osd_display_info("Zoom In");
	     break;
	   case XK_Z:
	     change_zoom(-1,-1);
	     osd_display_info("Zoom Out");
	     break;
	   case XK_i:
           case XK_I:
             send_event(XINE_EVENT_INPUT_UP);
             break;
           case XK_k:
           case XK_K:
             send_event(XINE_EVENT_INPUT_RIGHT);
             break;
           case XK_j:
           case XK_J:
             send_event(XINE_EVENT_INPUT_LEFT);
             break;
           case XK_m:
           case XK_M:
             send_event(XINE_EVENT_INPUT_DOWN);
             break;
	   case XK_o:
	   case XK_O:
	     print_osd_status();
	     break;
           case XK_Return:
             send_event(XINE_EVENT_INPUT_SELECT);
             break;
           case XK_F1:
             send_event(XINE_EVENT_INPUT_MENU1);
             break;
           case XK_F2:
             send_event(XINE_EVENT_INPUT_MENU2);
             break;
           case XK_F3:
             send_event(XINE_EVENT_INPUT_MENU3);
             break;
           case XK_F4:
             send_event(XINE_EVENT_INPUT_MENU4);
             break;
           case XK_F5:
             send_event(XINE_EVENT_INPUT_MENU5);
             break;
       	}
      }
         break;

       case Expose:
         if(xevent.xexpose.count != 0)
           break;
         xine_gui_send_vo_data(cxfe.stream, XINE_GUI_SEND_EXPOSE_EVENT, &xevent);
         break;

       case ConfigureNotify:
         {
	   XConfigureEvent *cev = (XConfigureEvent *) &xevent;
	   Window           tmp_win;

	   width  = cev->width;
	   height = cev->height;
	   if((cev->x == 0) && (cev->y == 0)) {
	     XLockDisplay(display);
	     XTranslateCoordinates(display, cev->window,
				   DefaultRootWindow(cev->display),
				   0, 0, &xpos, &ypos, &tmp_win);
	     XUnlockDisplay(display);
	   }
	   else {
	     xpos = cev->x;
	     ypos = cev->y;
	   }
         }
         break;

    }

       if(xevent.type == completion_event) {
	 xine_gui_send_vo_data(cxfe.stream, XINE_GUI_SEND_COMPLETION_EVENT, &xevent);
         }     
   }
}

char* makestring(char *string, int len)
{
   int i;
   char *p = (char *) malloc(len + 1);
   
   i = 0;
   while(len) {
        *(p + i++) = *string++;
        len--;
              }
   *(p + i) = '\0';
   return p;
}

int main(int argc, char **argv) {
  char              configfile[2048];
  x11_visual_t      vis;
  double            res_h, res_v;
  char             *vo_driver = "xv";
  char             *ao_driver = "auto";
  char             *mrl[1000];
  char             *cda_param = "input.cdda_device";
  char		   *dvd_param = "input.dvd_device";
  char             *vcd_param = "input.vcd_device";
  char             *input_device = "auto";
  char		   *ctrl_driver = "kybd";
  int               i,m=0;
  int 		    by_chapter;
  int 		    mrl_spec = FALSE;
  Atom              XA_NO_BORDER;
  MWMHints          mwmhints;
  int               x11=0;
  static            pthread_t lirc_thread;
  static	    pthread_t osd_thread;
  int               err;
  int dvd_mrl = 0;
  int vcd_mrl = 0;
  int cd_mrl = 0;
  /* default values */
  fullscreen = 0;
  
  
  if(argc <= 1) {
    printf("Please specify a mrl\n");
    printf("For help enter cxfe --help\n");
    return 1;
  }

  for(i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "-vo")) {
      vo_driver = argv[++i];
    }
    else if(!strcmp(argv[i], "-ao")) {
      ao_driver = argv[++i];
    }
    else if(!strcmp(argv[i], "-dev")) {
      input_device = argv[++i];
    }
    else if(!strcmp(argv[i], "-i")) {
      ctrl_driver = argv[++i];
    }
    else if(!strcmp(argv[i], "-fs")) {
      fullscreen = 1;
    }
    else if(!strcmp(argv[i], "-d")) {
      cxfe.deinterlace_enable = 1;
    }
    else if(!strcmp(argv[i], "-p")) {
      cxfe.custom_deint_plugin = 1;
      default_deinterlacer = argv[++i];
    }  
    /*else if(!strcmp(argv[i], "-p")) {
      if(!pplugins_num)
	 pplugins = (char **) xine_xmalloc(sizeof(char *) * 2);
      else
         pplugins = (char **) realloc(pplugins, sizeof(char *) * (pplugins_num + 2));
			  
      pplugins[pplugins_num] = argv[++i];
      pplugins_num++;
      pplugins[pplugins_num] = NULL;
    }*/
    else if(!strcmp(argv[i], "-v")) {
      printf("\nCXFE-Console Xine Frontend for DXR3/Xv/Xshm/fb v0.9.2 by Rett D. Walters\n");
      printf("Based on code from the Xine and Mplayer Projects.\n");
      exit(0);
      }
    else if(!strcmp(argv[i], "--help")) {
      printf("Current command line parameters:\n");
      printf("-vo <driver> Set video out driver (dxr3/xv/xshm/fb)\n");
      printf("-ao <driver> Set audio out driver (alsa/oss)\n");
      printf("-i <driver>  Set control driver (lirc) Keyboard is enabled at all times)\n");
      printf("-fs          Start cxfe in fullscreen\n");
      printf("<mrl>        Valid xine MRL (ex: dvd:/) Up to 1000 MRLs are allowed\n");
      printf("-v           Prints cxfe version and exits\n");
      printf("-d	   Start cxfe with deinterlacing plugins on\n");
      printf("-p	   Specify custom deinterlacing plugin to use\n");
      printf("--help       Prints this help\n");
      printf("Example: cxfe -vo xv -ao alsa -i lirc -dev /dev/dvd dvd:/)\n");
      exit(0);
      }
    else {
      mrl[m] = makestring(argv[i],strlen(argv[i]));
      // printf("MRL length:%d\n",strlen(argv[i]));
      mrl_spec = TRUE;
      m++;
      if (m > 1000) {
         printf("Error: Only 1000 MRLs are supported.\n");
	 exit(0);
	 }
    }
  }
  printf("\nCXFE-Console Xine Frontend for DXR3/Xv/Xshm/fb v0.9.2 by Rett D. Walters\n");
  printf("Based on code from the Xine and Mplayer Projects.\n");
  printf("Selected Video Driver:%s\n",vo_driver);
  printf("Selected Audio Driver:%s\n",ao_driver);
  printf("Current vo plugin settings:\n");
  printf("Hue: %d Saturation: %d Brightness: %d Contrast: %d\n",vo_hue,vo_saturation,vo_brightness,vo_contrast);
  
  if (m > 0) 
  	next_mrl = TRUE;
  // Was MRL provided in command line?

  if((!mrl_spec)) {
     printf("Please specify an mrl\n");
     return 1;
  }
  for(i = 0; i < m; i++){
     printf("mrl: '%s'\n", mrl[i]);
     }

  //printf("Length of videodriver name:%d\n",strlen(vo_driver));

  if ((!strcmp(vo_driver,"xv")) || (!strcmp(vo_driver,"xshm"))) {
      x11=1;
  }
  else if ((!strcmp(vo_driver,"fb")) || (!strcmp(vo_driver,"dxr3"))) {
      x11=0;
  }
  else {
      printf("Please specify a video driver using -vo <xv> <xshm> <dxr3> <fb>\n");
      return 1;
  }

  if (!strcmp(ctrl_driver,"lirc")) {
      lirc = 1;
      //printf("\nLIRC currently not supported.\n");
      }
  if(!XInitThreads()) {
    printf("XInitThreads() failed\n");
    return 1;
  }

  cxfe.xine = xine_new();
  sprintf(configfile, "%s%s", xine_get_homedir(), "/.xine/config");
  xine_config_load(cxfe.xine, configfile);
  xine_init(cxfe.xine);
  
    // check which mrl types was specified:
  for(i = 0; i < m; i++){
     if (strstr(mrl[i], "dvd:"))
       dvd_mrl = 1;
     else if (strstr(mrl[i], "vcd:"))
       vcd_mrl = 1;
     else if (strstr(mrl[i], "cd:"))
       cd_mrl = 1;
  }

  // Set input device
  if (strcmp(input_device , "auto")) {
     if (dvd_mrl) {
        config_update_string(dvd_param, input_device);
     }
     if (vcd_mrl) {
        config_update_string(vcd_param, input_device);
     }
     if (cd_mrl) {
        config_update_string(cda_param, input_device);
     }
  }

  if (x11) {
     display = XOpenDisplay(NULL);
     screen  = XDefaultScreen(display);
     xpos    = 0;
     ypos    = 0;
     width   = DEFAULT_WIDTH;
     height  = DEFAULT_HEIGHT;

     XLockDisplay(display);
     window[0] = XCreateSimpleWindow(display, XDefaultRootWindow(display),
				  xpos, ypos, width, height, 1, 0, 0);

     res_v = DisplayWidth(display, screen);
     res_h = DisplayHeight(display, screen);

     window[1] = XCreateSimpleWindow(display, XDefaultRootWindow(display),
				     0, 0, res_v, res_h, 0, 0, 0);

     if (fullscreen) {
       width = res_v;
       height = res_h;
     }

     XSelectInput(display, window[0], INPUT_MOTION);

     XSelectInput(display, window[1], INPUT_MOTION);

     XA_NO_BORDER         = XInternAtom(display, "_MOTIF_WM_HINTS", False);
     mwmhints.flags       = MWM_HINTS_DECORATIONS;
     mwmhints.decorations = 0;
     XChangeProperty(display, window[1],
		  XA_NO_BORDER, XA_NO_BORDER, 32, PropModeReplace, (unsigned char *) &mwmhints,
		  PROP_MWM_HINTS_ELEMENTS);

     XClassHint ch;
     ch.res_name = "cxfe";
     ch.res_class = "Cxfe";
     XTextProperty  w_name;
     w_name.value    = (unsigned char*)"cxfe";
     w_name.encoding = XA_STRING;
     w_name.format   = 8;
     w_name.nitems   = strlen(w_name.value);
     XSetWMProperties(display, window[0], &w_name, NULL, argv, argc, NULL, NULL, &ch);

     if(XShmQueryExtension(display) == True)
       completion_event = XShmGetEventBase(display) + ShmCompletion;
     else
       completion_event = -1;

     if (fullscreen)
       wsScreenSaverOff(display);

    XMapRaised(display, window[fullscreen]);

    XSync(display, True);
    XUnlockDisplay(display);

    vis.display           = display;
    vis.screen            = screen;
    vis.d                 = window[fullscreen];
    vis.dest_size_cb      = dest_size_cb;
    //if (x11)
    //  vis.frame_output_cb   = frame_output_cb_x11;
    //else
    vis.frame_output_cb   = frame_output_cb;
    vis.user_data         = NULL;
    pixel_aspect          = res_h / res_v;
    }


  if(fabs(pixel_aspect - 1.0) < 0.01)
      pixel_aspect = 1.0;

  if (x11)
      cxfe.vo_port = xine_open_video_driver(cxfe.xine, vo_driver, XINE_VISUAL_TYPE_X11, (void *) &vis);
  if (!strcmp(vo_driver,"dxr3")) {
      cxfe.vo_port = xine_open_video_driver(cxfe.xine, vo_driver, XINE_VISUAL_TYPE_X11, NULL);
      cxfe.deinterlace_capable = 0; 
      }
  if (!strcmp(vo_driver,"fb"))
      cxfe.vo_port = xine_open_video_driver(cxfe.xine, vo_driver, XINE_VISUAL_TYPE_FB, NULL);

  if (cxfe.vo_port == NULL) {
    printf("Failed to open output device\n");
    exit(0);
  }

  cxfe.ao_port = xine_open_audio_driver(cxfe.xine , ao_driver, NULL);

  cxfe.stream = xine_stream_new(cxfe.xine, cxfe.ao_port, cxfe.vo_port);
  event_queue = xine_event_new_queue(cxfe.stream);
  xine_event_create_listener_thread(event_queue, event_listener, NULL);

  if (x11){
     xine_gui_send_vo_data(cxfe.stream, XINE_GUI_SEND_DRAWABLE_CHANGED, (void *) window[fullscreen]);
     xine_gui_send_vo_data(cxfe.stream, XINE_GUI_SEND_VIDEOWIN_VISIBLE, (void *) 1);
     }

  if(pplugins_num) {
     char	**plugin = pplugins;
     
      while(*plugin) {
            pplugin_parse_and_store_post((const char *) *plugin);
            *plugin++;
          }

      pplugin_rewire_posts();
  }
  // if no deinterlacer specified, use default
  if (!cxfe.custom_deint_plugin)
      default_deinterlacer = DEFAULT_DEINTERLACER;
  
  if (cxfe.deinterlace_capable){
     post_deinterlace_init(NULL);
	
     if (cxfe.deinterlace_enable){
         printf("Starting Deinterlace\n");
         post_deinterlace();}
  }   
  
  i = 0;
  while ((next_mrl) && (i < m)) {
  if((!xine_open(cxfe.stream, mrl[i])) || (!xine_play(cxfe.stream, 0, 0))) {
    printf("Unable to open mrl '%s'\n", mrl[i]);
    return 1;
  }
  osd = xine_osd_new(cxfe.stream, 0,0,600,200);
  xine_osd_set_font(osd, "sans", 24);
  pthread_create(&osd_thread,0, osd_loop, 0);
  osd_display_info("    Playing MRL: %s",mrl[i]);
  
  /* get the volume */
  volume = xine_get_param(cxfe.stream, XINE_PARAM_AUDIO_VOLUME);

  // Load VO controls with initial values
  vo_hue = get_current_param(XINE_PARAM_VO_HUE);
  vo_saturation = get_current_param(XINE_PARAM_VO_SATURATION);
  vo_contrast = get_current_param(XINE_PARAM_VO_CONTRAST);
  vo_brightness = get_current_param(XINE_PARAM_VO_BRIGHTNESS);

    
  running = 1;

  
  by_chapter = xine_get_stream_info(cxfe.stream, XINE_STREAM_INFO_HAS_CHAPTERS);
  if (by_chapter)
  	printf("Chapters in stream.\n");

#ifdef HAVE_LIBLIRC_CLIENT
  if ((lirc)&&(i==0))
  {
        fd = lirc_init("cxfe",1);
        printf("Initializing LIRC.\n");
  }
  if (fd==-1)
  {      
            printf("LIRC Init failed, disabling LIRC.\n");
            lirc = 0;
  }
  if ((lirc)&&(i==0))
  {
        if(lirc_readconfig("~/.lircrc",&config,NULL)==0)
        	printf("LIRC initialization complete.\n");
  	else
  	{
  		printf("No user specific LIRC settings, loading cxfe defaults.\n");
        	if(lirc_readconfig("/usr/share/cxfe/lircrc",&config,NULL)==0)
		        printf("LIRC initialization complete.\n");
		else
		lirc = 0;
  	}        
	err = pthread_create(&lirc_thread, 0, process_lirc_thread, NULL);
	if(err) {
           printf("Failed to create lirc thread.\n");
	   lirc = 0;}  
  }      
#endif        
  if (!x11)
  	cxfe_run_console();
  else
      {
  	cxfe_run_x11();
      }

  i++;
  }
  
  // shutdown 
  if (cxfe.deinterlace_capable){
     cxfe.deinterlace_enable = 0;
     post_deinterlace();
  }
  // Let video output buffers flush
  sleep(2);
  pthread_cancel(osd_thread);
  if(osd_info_visible) {
     osd_info_visible = 0;
     xine_osd_hide(osd,0);
     }
  xine_osd_free(osd);
  xine_close(cxfe.stream);
  xine_event_dispose_queue(event_queue);
  xine_dispose(cxfe.stream);
  xine_close_audio_driver(cxfe.xine, cxfe.ao_port);
  xine_close_video_driver(cxfe.xine, cxfe.vo_port);
  xine_exit(cxfe.xine);
  
  if (x11) {
     XLockDisplay(display);
     wsScreenSaverOn(display);
     XUnmapWindow(display,  window[fullscreen]);
     XDestroyWindow(display,  window[0]);
     XDestroyWindow(display,  window[1]);
     XUnlockDisplay(display);
     XCloseDisplay (display);
  }
  printf("\nExitting!\n");
#ifdef HAVE_LIBLIRC_CLIENT
  if (lirc)
  {
     pthread_cancel(lirc_thread);
     lirc_freeconfig(config);
     lirc_deinit();
  }   
#endif     
  return 1;
}

