/* 
 * Copyright (C) 2003 by Fredrik Noring
 * 
 * This file is part of xine, a unix video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * Initial version by Fredrik Noring January 2003.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <pthread.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/dpms.h>
#include <xine.h>
#include <xine/xineutils.h>

//#include "callback.h"
#define TRUE 1
#define FALSE 0


typedef struct {
  xine_post_t    *post;
  char           *name;
} post_element_t;

struct cxfe {
   xine_video_port_t   	    *vo_port;
   xine_audio_port_t        *ao_port;
   post_element_t          **post_elements;
   int                       post_elements_num;
   int                       post_enable;
   xine_t              	    *xine;
   xine_stream_t            *stream;
   char                     *deinterlace_plugin;
   post_element_t          **deinterlace_elements;
   int                       deinterlace_elements_num;
   int                       deinterlace_enable;
   int			     deinterlace_capable;
   int                       custom_deint_plugin;
};

extern struct cxfe cxfe;

//static xine_t              *xine;
//static xine_stream_t       *stream;
//static xine_post_out_t     *vo_source;
//static xine_post_out_t     *ao_source;
//static xine_post_in_t      *input;
//static xine_post_t	   *tvtime;
//static int                  movie_height = 0;
//static int                  movie_width = 0;
char			   *default_deinterlacer;
#ifdef HAVE_LIBLIRC_CLIENT
static char		   *code;
static char		   *c;
static int	            lirc_ret;

#endif
