/*
 * Navit networking services
 *
 * (c) Alexander Vdolainen 2016 <avdolainen@zoho.com>
 *
 * navit is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * navit is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>

#include <tdata/usrtc.h>
#include <tdata/list.h>
#include <sxmp/sxmp.h>
#include <ydaemon/ydaemon.h>
#define DMUX_USE 1
#include <ydaemon/dataobject.h>

#define MOD_PREFIX  datamux

/* limits */
#define MAX_DATAOBJECTS  65535

/* a few globals */
static obj_store_t *ostore = NULL;
static usrtc_t *dox_store = NULL;
static usrtc_t *object_store = NULL;

struct dm_onode {
  union {
    dataobject_t *dmo;
    domx_t *domx;
  };
  usrtc_node_t node;
};

static long __cmp_cstr(const void *a, const void *b)
{
  return (long)strcmp((const char *)a, (const char *)b);
}

static inline int __isdigits(const char *val)
{
  register char c;
  register int i;

  for(c = val[0], i = 0; c != '\0'; i++, c = val[i])
    if(!isdigit((int)c)) return 1; /* not falls into digits, fail */

  return 0;
}

/* syntax defines */
/* define the object: (dm-define-object '<name> (:<item-name> '<type> len) ... ) */
#define DATAMUXSYN_DEFINE_OBJECT  "dm-define-object"
/* { types */
#define DATAMUXSYN_U8     "u8"
#define DATAMUXSYN_S8     "s8"
#define DATAMUXSYN_U16    "u16"
#define DATAMUXSYN_S16    "s16"
#define DATAMUXSYN_U32    "u32"
#define DATAMUXSYN_S32    "s32"
#define DATAMUXSYN_U64    "u64"
#define DATAMUXSYN_S64    "s64"
#define DATAMUXSYN_CSTR   "cstr"
#define DATAMUXSYN_BLOB   "blob"
#define DATAMUXSYN_TACRT  "access-rights"
/* } */
/* (dm-sxmpchannel-add '<object-name> (:sxmp-instance '<sxmp-instance-name>)
 * (:sxmp-channel-typeid <typeid>)
 * (:access-filter '<no-filter|domain-filter|userdac-filter|full-filter>)) */
#define DATAMUXSYN_SXMPCHANNEL_ADD  "dm-sxmpchannel-add"
/* { syntax defines */
#define DATAMUXSYN_SXMPINSTANCE    "sxmp-instance"
#define DATAMUXSYN_SXMPCHANNELTID  "sxmp-channel-typeid"
#define DATAMUXSYN_ACCESSFILTER    "access-filter" /* { filter types */
#define DATAMUXSYN_NOFILTER       "no-filter"
#define DATAMUXSYN_DOMAINFILTER   "domain-filter"
#define DATAMUXSYN_USERDACFILTER  "userdac-filter"
#define DATAMUXSYN_FULLFILTER     "full-filter"
/*    } */
/* } */
/* set a storage backend for the object (dm-set-object-store-backend '<name>
 *                                       (:be (object '<> <>))(:key <>))
 */
#define DATAMUXSYN_SET_OBJECTBE  "dm-set-object-store-backend"
/* syntax defines { */
#define DATAMUXSYN_BE   "be"
#define DATAMUXSYN_KEY  "key"
/* } */
/* create and set length of the cache for data object:
 * (dm-set-object-cache '<name> <len>kb|mb)
 */
#define DATAMUXSYN_SET_CACHE  "dm-set-object-cache"

/* shared objects typedefs */
typedef int (*rpc_func_t)(void *, sexp_t *);

static scret_t __datamux_set_object_cache(yd_context_t *ctx, sexp_t *sx,
                                          void *priv)
{
  register int state = 0;
  register int idx;
  int r = EINVAL;
  domx_t *sdomx = NULL;
  struct dm_onode *onode;
  usrtc_node_t *unode;
  sexp_t *isx;
  char *tbuf;
  scret_t rets;
  size_t cachesize = 0;
  int mp = 0; /* 0 - no, 1 - kb, 2 - mb */

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) { RETURN_SRET_IRES(rets, r); }

    switch(state) {
    case 0:
      if(strcmp(isx->val, DATAMUXSYN_SET_CACHE)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1:
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }

      /* find data object */
      if(!(unode = usrtc_lookup((usrtc_t *)priv, (const char *)isx->val)))
        { r = ENOENT; RETURN_SRET_IRES(rets, r); }
      else {
        onode = (struct dm_onode *)usrtc_node_getdata(unode);
        sdomx = onode->domx;
        if(!sdomx) { r = ENOENT; RETURN_SRET_IRES(rets, r); }
      }
      state++;
      break;
    case 2:
      if(isx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); }

      cachesize = strtol((const char *)isx->val, &tbuf, 10);
      if(tbuf == isx->val) { RETURN_SRET_IRES(rets, r); }
      if(*tbuf != '\0') {
        if(!strcmp(tbuf, "kb")) mp = 1;
        else if(!strcmp(tbuf, "mb")) mp = 2;
        else mp = -1;
      } else mp = 0;

      break;
    }
  }

  if(mp < 0) { RETURN_SRET_IRES(rets, r); }
  else if(mp == 1) cachesize *= 1024;
  else if(mp == 2) cachesize *= (1024*1024);

  /* check for cachesize minimal value */
  if(!mp && cachesize < 4096) cachesize = 4096;

  /* let's a deal */
  r = domx_set_cache(sdomx, cachesize);

  RETURN_SRET_IRES(rets, r);
}

static scret_t __datamux_set_object_store_backend(yd_context_t *ctx, sexp_t *sx,
                                                  void *priv)
{
  register int state = 0;
  register int idx;
  register int iidx;
  register int tstate = -1;
  int r = EINVAL;
  domx_t *sdomx;
  struct dm_onode *onode;
  usrtc_node_t *unode;
  sexp_t *isx, *imsx;
  char *tbuf, *key = NULL;
  void *beptr = NULL;
  ydc_conf_val_t *rval;
  scret_t rets, irets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 2) { RETURN_SRET_IRES(rets, r); }

    switch(state) {
    case 0:
      if(strcmp(isx->val, DATAMUXSYN_SET_OBJECTBE)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1:
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }

      /* find data object */
      if(!(unode = usrtc_lookup((usrtc_t *)priv, (const char *)isx->val)))
        { r = ENOENT; RETURN_SRET_IRES(rets, r); }
      else {
        onode = (struct dm_onode *)usrtc_node_getdata(unode);
        sdomx = onode->domx;
        if(!sdomx) { r = ENOENT; RETURN_SRET_IRES(rets, r); }
      }
      state++;
      break;
    case 2:
      if(isx->ty != SEXP_LIST) goto __fini;
      else tstate = -1;

      SEXP_ITERATE_LIST(isx, imsx, iidx) {
        if(!iidx) {
          if(imsx->ty == SEXP_LIST) goto __fini;

          /* check other syntax rules */
          if((imsx->aty != SEXP_BASIC) || (*(imsx->val) != ':')) goto __fini;
          else tbuf = imsx->val + sizeof(char);

          if(!strcmp(tbuf, DATAMUXSYN_BE)) tstate = 0;
          else if(!strcmp(tbuf, DATAMUXSYN_KEY)) tstate = 1;
          else goto __fini;
        } else {
          if(tstate && (imsx->ty == SEXP_LIST)) goto __fini;
          else if(!tstate && (imsx->ty != SEXP_LIST)) goto __fini;

          if(!tstate) {
            irets = yd_eval_sexp(ctx, imsx);
            if(irets.type != SCOBJECTPTR) { RETURN_SRET_IRES(rets, r); }

            beptr = irets.ret;
          } else {
            if(imsx->aty == SEXP_DQUOTE) key = imsx->val;
            else if(imsx->aty == SEXP_BASIC && strchr(imsx->val, '/')) {
              if((r = ydc_conf_get_val(ctx->values, (const char *)imsx->val, &rval)))
                { RETURN_SRET_IRES(rets, r); }
              else r = EINVAL; /* reset error code */

              if(rval->type != STRING) goto __fini;

              key = (char *)rval->value;
            } else goto __fini;
          }
        }
      }
      break;
    }
  }

  /* let's a deal */
  if(beptr) r = domx_set_be(sdomx, beptr, (const char *)key);

 __fini:

  RETURN_SRET_IRES(rets, r);
}

static scret_t __datamux_sxmpchannel_add(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  register int iidx;
  register int tstate = -1;
  int r = EINVAL;
  int chtid = -1, filt = -1;
  char *insname = NULL, *tbuf = NULL;
  sexp_t *isx, *imsx;
  struct dm_onode *domxnode = NULL;
  domx_t *domx = NULL;
  usrtc_node_t *unode = NULL;
  scret_t rets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 2) { RETURN_SRET_IRES(rets, r); }

    switch(state) {
    case 0:
      if(strcmp(isx->val, DATAMUXSYN_SXMPCHANNEL_ADD)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1:
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }

      /* check for redefinition */
      if(!(unode = usrtc_lookup((usrtc_t *)priv, (const char *)isx->val)))
        { r = ENOENT; RETURN_SRET_IRES(rets, r); }
      else {
        domxnode = (struct dm_onode *)usrtc_node_getdata(unode);
        domx = domxnode->domx;
        if(!domx) { RETURN_SRET_IRES(rets, r); }
      }
      state++;
      break;
    case 2:
      if(isx->ty != SEXP_LIST) goto __fini;
      tstate = -1;

      /* now we're able to read the item data */
      SEXP_ITERATE_LIST(isx, imsx, iidx) {
        if(imsx->ty == SEXP_LIST) goto __fini;

        if(!iidx) {
          if((imsx->aty != SEXP_BASIC) || (*(imsx->val) != ':')) goto __fini;
          else tbuf = imsx->val + sizeof(char);

          if(!strcmp(tbuf, DATAMUXSYN_SXMPINSTANCE)) tstate = 0;
          else if(!strcmp(tbuf, DATAMUXSYN_SXMPCHANNELTID)) tstate = 1;
          else if(!strcmp(tbuf, DATAMUXSYN_ACCESSFILTER)) tstate = 2;
          else goto __fini;
        } else if(iidx == 1) {
          /* recognize other parameters */
          switch(tstate) {
          case 0:
            if(imsx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
            insname = imsx->val;
            break;
          case 1:
            if(imsx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); }
            if(__isdigits((const char *)imsx->val)) { RETURN_SRET_IRES(rets, r); }

            chtid = atoi(imsx->val);
            break;
          case 2:
            if(imsx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }

            if(!strcmp(imsx->val, DATAMUXSYN_NOFILTER)) filt = YDM_NO_FILTER;
            else if(!strcmp(imsx->val, DATAMUXSYN_DOMAINFILTER)) filt = YDM_DOMAIN_FILTER;
            else if(!strcmp(imsx->val, DATAMUXSYN_USERDACFILTER)) filt = YDM_USERDAC_FILTER;
            else if(!strcmp(imsx->val, DATAMUXSYN_FULLFILTER)) filt = YDM_FULL_FILTER;
            else goto __fini;
            break;
          default: goto __fini; break;
          }
        } else goto __fini;
      }
      break;
    }
  }

  if(!insname) goto __fini;
  if(filt == -1 || chtid == -1) goto __fini;

  r = domx_set_sxmpchannel(domx, (const char *)insname, chtid, filt);

 __fini:

  RETURN_SRET_IRES(rets, r);
}

static scret_t __datamux_define_object(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  register int iidx;
  int r = EINVAL;
  int len = -1, type, dlen;
  sexp_t *isx, *imsx;
  dataobject_t *dmobject = NULL;
  char *iname, *itype;
  struct dm_onode *dnode = NULL, *domxnode = NULL;
  domx_t *domx = NULL;
  usrtc_node_t *unode = NULL;
  scret_t rets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 2) { RETURN_SRET_IRES(rets, r); }

    switch(state) {
    case 0:
      if(strcmp(isx->val, DATAMUXSYN_DEFINE_OBJECT)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1: /* first do a syntax check */
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }

      /* check for redefinition */
      if((unode = usrtc_lookup((usrtc_t *)priv, (const char *)isx->val)))
        { r = EEXIST; RETURN_SRET_IRES(rets, r); }

      if(!(dmobject = dotr_create((const char *)isx->val))) { r = ENOMEM; goto __fini; }
      state ++; break;
    case 2:
      if(isx->ty != SEXP_LIST) goto __fini;
      iname = itype = NULL; len = -1; /* reset values */

      /* now we're able to read the item data */
      SEXP_ITERATE_LIST(isx, imsx, iidx) {
        if(imsx->ty == SEXP_LIST) goto __fini;

        switch(iidx) {
        case 0: /* item name */
          if((imsx->aty != SEXP_BASIC) || (*(imsx->val) != ':')) goto __fini;
          iname = imsx->val + sizeof(char);          break;
        case 1: /* type */
          if(imsx->aty != SEXP_SQUOTE) goto __fini;
          itype = imsx->val;          break;
        case 2: /* lenght */
          if(imsx->aty != SEXP_BASIC) goto __fini;
          len = atoi(imsx->val); break;
        default: /* something wrong */
          goto __fini; break;
        }
      }

      /* time to add an item to the list */
      if(!iname || !itype) goto __fini; /* all values are set */

      /* type workout */
      if(!strcmp(itype, DATAMUXSYN_U8)) type = U8;
      else if(!strcmp(itype, DATAMUXSYN_S8)) type = S8;
      else if(!strcmp(itype, DATAMUXSYN_U16)) type = U16;
      else if(!strcmp(itype, DATAMUXSYN_S16)) type = S16;
      else if(!strcmp(itype, DATAMUXSYN_U32)) type = U32;
      else if(!strcmp(itype, DATAMUXSYN_S32)) type = S32;
      else if(!strcmp(itype, DATAMUXSYN_U64)) type = U64;
      else if(!strcmp(itype, DATAMUXSYN_S64)) type = S64;
      else if(!strcmp(itype, DATAMUXSYN_CSTR)) type = CSTR;
      else if(!strcmp(itype, DATAMUXSYN_BLOB)) type = TBLOB;
      else if(!strcmp(itype, DATAMUXSYN_TACRT)) type = TACRT;
      else goto __fini;

      if(len == -1) { /* lenght isn't defined */
        if(type == CSTR || type == BLOB) goto __fini; /* will not works */

        dlen = tltable[type].len;
      } else {
        if(type == TACRT) goto __fini; /* access right shouldn't has a size */

        if(type == CSTR || type == BLOB) dlen = len;
        else dlen = tltable[type].len * len; /* looks like an array */
      }

      if((r = dotr_set_item(dmobject, type, dlen, (const char *)iname))) goto __fini;

      break;
    default:      goto __fini; break;
    }
  }

  if(!(dnode = malloc(sizeof(struct dm_onode)))) r = ENOMEM;
  else {
    if(usrtc_count((usrtc_t *)priv) >= MAX_DATAOBJECTS) { r = EFBIG; goto __fini; } /* amount */

    usrtc_node_init(&dnode->node, dnode);
    dnode->dmo = dmobject;

    /* allocate executive structures */
    if(!(domx = malloc(sizeof(domx_t)))) { r = ENOMEM; goto __fini; }
    /* domxnode */
    if(!(domxnode = malloc(sizeof(struct dm_onode)))) { r = ENOMEM; goto __fini; }

    /* init all */
    domx_init(domx, dmobject);
    usrtc_node_init(&domxnode->node, domxnode);
    domxnode->domx = domx;

    /* insert all */
    usrtc_insert((usrtc_t *)priv, &dnode->node, (void *)dmobject->name);
    usrtc_insert(dox_store, &domxnode->node, (void *)domx->name);
    /* provide object - domx */
    obj_store_set((obj_store_t *)ostore, domx->name, (void *)domx);
    r = 0; /* we've passed it all */
  }

 __fini:
  if(r) {
    if(dmobject) dotr_destroy(dmobject);
    if(dnode) free(dnode);
    if(domx) free(domx);
  }

  RETURN_SRET_IRES(rets, r);
}

def_shutdown(datamux) {
  return 0;
}

def_preinit(datamux) {
  int r = 0;

  if(!(ostore = malloc(sizeof(obj_store_t)))) return ENOMEM;
  else r = obj_store_init(ostore);

  if(r) return r;

  /* object_store */
  if(!(object_store = malloc(sizeof(usrtc_t)))) return ENOMEM;
  usrtc_init(object_store, USRTC_REDBLACK, MAX_DATAOBJECTS, __cmp_cstr);
  /* domx store */
  if(!(dox_store = malloc(sizeof(usrtc_t)))) return ENOMEM;
  usrtc_init(dox_store, USRTC_SPLAY, MAX_DATAOBJECTS, __cmp_cstr);

  /* add special functions */
  scm_func_tree_insert(ctx, DATAMUXSYN_DEFINE_OBJECT, __datamux_define_object,
                       (void *)object_store);
  scm_func_tree_insert(ctx, DATAMUXSYN_SXMPCHANNEL_ADD, __datamux_sxmpchannel_add,
                       (void *)dox_store);
  scm_func_tree_insert(ctx, DATAMUXSYN_SET_OBJECTBE, __datamux_set_object_store_backend,
                       (void *)dox_store);
  scm_func_tree_insert(ctx, DATAMUXSYN_SET_CACHE, __datamux_set_object_cache,
                       (void *)dox_store);


  return r;
}

def_init(datamux) {
  /* store objects */

  return 0;
}

def_run(datamux) {
  return 0;
}

def_getobject(datamux) {
  void *obj = NULL;
  int r = obj_store_get(ostore, oname, &obj);

  if(r)
    return NULL;
  else return obj;
}

