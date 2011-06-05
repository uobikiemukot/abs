/* 
 * Copyright (C) 2003 by Dirk Meyer
 * Cxfe Modifications Copyright(C) 2004 by Rett Walters
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
 * The code is taken from xine-ui/src/fb/post.c at changed to work with cxfe
 */

#include "post.h"
#include "main.h"


typedef struct {
  xine_post_t                 *post;
  xine_post_api_t             *api;
  xine_post_api_descr_t       *descr;
  xine_post_api_parameter_t   *param;
  char                        *param_data;

  int                          x;
  int                          y;

  int                          readonly;

  char                       **properties_names;
} post_object_t;


static int __pplugin_retrieve_parameters(post_object_t *pobj) {
  xine_post_in_t             *input_api;
  
  printf("__pplugin_retrieve_parameters\n");
  if((input_api = (xine_post_in_t *) xine_post_input(pobj->post, "parameters"))) {
    xine_post_api_t            *post_api;
    xine_post_api_descr_t      *api_descr;
    xine_post_api_parameter_t  *parm;
    int                         pnum = 0;
    
    post_api = (xine_post_api_t *) input_api->data;
    
    api_descr = post_api->get_param_descr();
    
    parm = api_descr->parameter;
    pobj->param_data = malloc(api_descr->struct_size);
    
    while(parm->type != POST_PARAM_TYPE_LAST) {
      
      post_api->get_parameters(pobj->post, pobj->param_data);
      
      if(!pnum)
	pobj->properties_names = (char **) xine_xmalloc(sizeof(char *) * 2);
      else
	pobj->properties_names = (char **) 
	  realloc(pobj->properties_names, sizeof(char *) * (pnum + 2));
      
      pobj->properties_names[pnum]     = strdup(parm->name);
      pobj->properties_names[pnum + 1] = NULL;
      pnum++;
      parm++;
    }
    
    pobj->api      = post_api;
    pobj->descr    = api_descr;
    pobj->param    = api_descr->parameter;
    
    return 1;
  }

  return 0;
}

static void _pplugin_update_parameter(post_object_t *pobj) {
  printf("_pplugin_update_parameter\n");
  pobj->api->set_parameters(pobj->post, pobj->param_data);
  pobj->api->get_parameters(pobj->post, pobj->param_data);
}


/* -post <name>:option1=value1,option2=value2... */
static post_element_t **pplugin_parse_and_load(const char *pchain, int *post_elements_num) {
  post_element_t **post_elements = NULL;
  char            *post_chain;

  *post_elements_num = 0;
  
  printf("**pplugin_parse_and_load\n");
  //printf("pchain:%s\n",pchain);
  if(pchain && strlen(pchain)) {
    char *p;
    
    xine_strdupa(post_chain, pchain);
    
    while((p = xine_strsep(&post_chain, ";"))) {
      
      if(p && strlen(p)) {
	char          *plugin, *args = NULL;
	xine_post_t   *post;
	
	while(*p == ' ')
	  p++;
	
	plugin = strdup(p);
	
	if((p = strchr(plugin, ':')))
	  *p++ = '\0';
	
	if(p && (strlen(p) > 1))
	  args = p;
	//printf("Plugin:%s\n",plugin);
	post = xine_post_init(cxfe.xine, plugin, 0, &cxfe.ao_port, &cxfe.vo_port);
	
	if(post) {
	  post_object_t  pobj;
	 
	  if(!(*post_elements_num))
	    post_elements = (post_element_t **) xine_xmalloc(sizeof(post_element_t *) * 2);
	  else
	    post_elements = (post_element_t **) 
	      realloc(post_elements, sizeof(post_element_t *) * ((*post_elements_num) + 2));
	  
	  post_elements[(*post_elements_num)] = (post_element_t *) 
	    xine_xmalloc(sizeof(post_element_t));
	  post_elements[(*post_elements_num)]->post = post;
	  post_elements[(*post_elements_num)]->name = strdup(plugin);
	  (*post_elements_num)++;
	  post_elements[(*post_elements_num)] = NULL;
	  
	  memset(&pobj, 0, sizeof(post_object_t));
	  pobj.post = post;
	  
	  if(__pplugin_retrieve_parameters(&pobj)) {
	    int   i;
	    
	    if(pobj.properties_names && args) {
	      char *param;
	      
	      while((param = xine_strsep(&args, ",")) != NULL) {
		
		p = param;
		
		while((*p != '\0') && (*p != '='))
		  p++;
		
		if(p && strlen(p)) {
		  int param_num = 0;
		  
		  *p++ = '\0';
		  
		  while(pobj.properties_names[param_num]
			&& strcasecmp(pobj.properties_names[param_num], param))
		    param_num++;
		  
		  if(pobj.properties_names[param_num]) {
		    
		    pobj.param    = pobj.descr->parameter;
		    pobj.param    += param_num;
		    pobj.readonly = pobj.param->readonly;
		    
		    switch(pobj.param->type) {
		    case POST_PARAM_TYPE_INT:
		      if(!pobj.readonly) {
			if(pobj.param->enum_values) {
			  char **values = pobj.param->enum_values;
			  int    i = 0;
	  
			  while(values[i]) {
			    if(!strcasecmp(values[i], p)) {
			      *(int *)(pobj.param_data + pobj.param->offset) = i;
			      break;
			    }
			    i++;
			  }

			  if( !values[i] ) 
			    *(int *)(pobj.param_data + pobj.param->offset) = (int) strtol(p, &p, 10);
			} else {
			  *(int *)(pobj.param_data + pobj.param->offset) = (int) strtol(p, &p, 10);
			}
			_pplugin_update_parameter(&pobj);
		      }
		      break;
		      
		    case POST_PARAM_TYPE_DOUBLE:
		      if(!pobj.readonly) {
			*(double *)(pobj.param_data + pobj.param->offset) = strtod(p, &p);
			_pplugin_update_parameter(&pobj);
		      }
		      break;
		      
		    case POST_PARAM_TYPE_CHAR:
		    case POST_PARAM_TYPE_STRING:
		      if(!pobj.readonly) {
			if(pobj.param->type == POST_PARAM_TYPE_CHAR) {
			  int maxlen = pobj.param->size / sizeof(char);
			  
			  snprintf((char *)(pobj.param_data + pobj.param->offset), maxlen, "%s", p);
			  _pplugin_update_parameter(&pobj);
			}
			else
			  fprintf(stderr, "parameter type POST_PARAM_TYPE_STRING not supported yet.\n");
		      }
		      break;
		      
		    case POST_PARAM_TYPE_STRINGLIST: /* unsupported */
		      if(!pobj.readonly)
			fprintf(stderr, "parameter type POST_PARAM_TYPE_STRINGLIST not supported yet.\n");
		      break;
		      
		    case POST_PARAM_TYPE_BOOL:
		      if(!pobj.readonly) {
			*(int *)(pobj.param_data + pobj.param->offset) = ((int) strtol(p, &p, 10)) ? 1 : 0;
			_pplugin_update_parameter(&pobj);
		      }
		      break;
		    }
		  }
		}
	      } 
	      
	      i = 0;
	      
	      while(pobj.properties_names[i]) {
		free(pobj.properties_names[i]);
		i++;
	      }
	      
	      free(pobj.properties_names);
	    }
	    
	    free(pobj.param_data);
	  }
	}
	
	free(plugin);
      }
    }
  }

  return post_elements;
}


void pplugin_parse_and_store_post(const char *post_chain) {
  post_element_t **posts = NULL;
  int              num;

  printf("pplugin_parse_and_store_post\n");
  if((posts = pplugin_parse_and_load(post_chain, &num))) {
    //printf("post_elements_num:%d\n",cxfe.post_elements_num);
    if(cxfe.post_elements_num) {
      int i;
      int ptot = cxfe.post_elements_num + num;
      
      cxfe.post_elements = (post_element_t **) realloc(cxfe.post_elements, 
							sizeof(post_element_t *) * (ptot + 1));
      for(i = cxfe.post_elements_num; i <  ptot; i++){
	cxfe.post_elements[i] = posts[i - cxfe.post_elements_num];
	//printf("cxfe.post_elements[%d]:%s\n",i,cxfe.post_elements[i]);
	}
        
      cxfe.post_elements[i] = NULL;
      cxfe.post_elements_num += num;

    }
    else {
      cxfe.post_elements     = posts;
      cxfe.post_elements_num = num;;
    }
  }
  
}


static void _pplugin_unwire(void) {
  xine_post_out_t  *vo_source;
  
  printf("_pplugin_unwire\n");
  vo_source = xine_get_video_source(cxfe.stream);

  (void) xine_post_wire_video_port(vo_source, cxfe.vo_port);
}


static void _pplugin_rewire_from_post_elements(post_element_t **post_elements, int post_elements_num) {
  
  printf("_pplugin_rewire_from_post_elements\n");
  if(post_elements_num) {
    xine_post_out_t   *vo_source;
    int                i = 0;
    
    //printf("post_elements_num:%d\n",post_elements_num);
    
    for(i = (post_elements_num - 1); i >= 0; i--) {
      
      const char *const *outs = xine_post_list_outputs(post_elements[i]->post);
      const xine_post_out_t *vo_out = xine_post_output(post_elements[i]->post, (char *) *outs);
      if(i == (post_elements_num - 1)) {
	xine_post_wire_video_port((xine_post_out_t *) vo_out, cxfe.vo_port);
      }
      else {
	const xine_post_in_t *vo_in = xine_post_input(post_elements[i + 1]->post, "video");
	int                   err;
	
	err = xine_post_wire((xine_post_out_t *) vo_out, (xine_post_in_t *) vo_in);	
      }
    }
    
    vo_source = xine_get_video_source(cxfe.stream);
    xine_post_wire_video_port(vo_source, post_elements[0]->post->video_input[0]);
  }
}


static post_element_t **_pplugin_join_deinterlace_and_post_elements(int *post_elements_num) {
  post_element_t **post_elements;
  int i, j;

  printf("**_pplugin_join_deinterlace_and_post_elements\n");
  *post_elements_num = 0;
  if( cxfe.post_enable )
    *post_elements_num += cxfe.post_elements_num;
  if( cxfe.deinterlace_enable )
    *post_elements_num += cxfe.deinterlace_elements_num;

  if( *post_elements_num == 0 )
    return NULL;

  post_elements = (post_element_t **) 
    xine_xmalloc(sizeof(post_element_t *) * (*post_elements_num));

  for( i = 0; cxfe.deinterlace_enable && i < cxfe.deinterlace_elements_num; i++ ) {
    post_elements[i] = cxfe.deinterlace_elements[i];
  }

  for( j = 0; cxfe.post_enable && j < cxfe.post_elements_num; j++ ) {
    //printf("cxfe.post_elements_num:%d\n",cxfe.post_elements_num);
    //printf("cxfe.post_element=%s\n",cxfe.post_elements[j]);
    post_elements[i+j] = cxfe.post_elements[j];
  }
  
  return post_elements;
}

static void _pplugin_rewire(void) {

  static post_element_t **post_elements;
  int post_elements_num;

  printf("_pplugin_rewire\n");
  post_elements = _pplugin_join_deinterlace_and_post_elements(&post_elements_num);

  if( post_elements ) {
    _pplugin_rewire_from_post_elements(post_elements, post_elements_num);

    free(post_elements);
  }
}



//#define DEFAULT_DEINTERLACER //"tvtime:method=greedy2frame,enabled=1,pulldown=none,framerate_mode=full,judder_correction=0,cheap_mode=0//"

#if 0  /* FIXME: unused? */
static void post_deinterlace_plugin_cb(void *data, xine_cfg_entry_t *cfg) {
  post_element_t **posts = NULL;
  int              num, i;
  
  fbxine.deinterlace_plugin = cfg->str_value;
  
  if(fbxine.deinterlace_enable)
    _pplugin_unwire();
  
  for(i = 0; i < fbxine.deinterlace_elements_num; i++) {
    xine_post_dispose(fbxine.xine, fbxine.deinterlace_elements[i]->post);
    free(fbxine.deinterlace_elements[i]->name);
    free(fbxine.deinterlace_elements[i]);
  }
  
  SAFE_FREE(fbxine.deinterlace_elements);
  fbxine.deinterlace_elements_num = 0;
  
  if((posts = pplugin_parse_and_load(fbxine.deinterlace_plugin, &num))) {
    fbxine.deinterlace_elements     = posts;
    fbxine.deinterlace_elements_num = num;
  }

  if(fbxine.deinterlace_enable)
    _pplugin_rewire();
}
#endif



void post_deinterlace_init(const char *deinterlace_post) {
  post_element_t **posts = NULL;
  int              num;

  printf("post_deinterlace_init\n");
  cxfe.deinterlace_plugin = default_deinterlacer;
  
  if((posts = pplugin_parse_and_load((deinterlace_post && strlen(deinterlace_post)) ? 
				     deinterlace_post : cxfe.deinterlace_plugin, &num))) {
    cxfe.deinterlace_elements     = posts;
    cxfe.deinterlace_elements_num = num;
  }
}


void post_deinterlace(void) {
  
  printf("post_deinterlace\n");
  if( !cxfe.deinterlace_elements_num ) {
    /* fallback to the old method */
    xine_set_param(cxfe.stream, XINE_PARAM_VO_DEINTERLACE,
                   cxfe.deinterlace_enable);
  }
  else {
    _pplugin_unwire();
    _pplugin_rewire();
  }
}

void pplugin_rewire_posts(void) {
  
  printf("pluggin_rewire_posts\n");
  _pplugin_unwire();
  _pplugin_rewire();
}

/* end of post.c */
  
