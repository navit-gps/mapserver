/*
 * Libmapdiff is a simple C library specially designed to be used for
 * navit map format patches (make/apply patch functions).
 *
 * (c) Alexander Vdolainen 2016 <avdolainen@zoho.com>
 *
 * libmapdiff is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libmapdiff is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 *
 */

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#include <mpdiff/mpdiff.h>

navit_mapit_t *navit_mapit_cc(struct nmit_ops *nm, void *opts)
{
  navit_mapit_cc *n = NULL;
  int r = 0;

  if(!nm) {
    errno = EINVAL;
    goto __failfi;
  }

  if(!(n = malloc(sizeof(navit_mapit_t)))) {
    errno = ENOMEM;
    goto failfi;
  }

  /* init all the stuff */
  memset(n, 0, sizeof(navit_mapit_t));
  n->ops = nm;
  if((r = n->ops->create(n, opts))) {
    errno = r;
    goto __failfi;
  } else errno = 0; /* just for the case */

  return n;

 __failfi:
  if(n) free(n);
  return NULL;
}

int navit_mapit_free(navit_mapit_t *nm)
{
  int r = 0;

  /* avoid segfs */
  if(!nm || !nm->ops) return EINVAL;
  if(!nm->ops->free) return EINVAL;

  if((r = nm->ops->free(nm))) return r;

  free(nm);

  return 0;
}

size_t mpdiff_cmp(navit_mapit_t *m1, navit_mapit_t *m2, navit_mpdiff_t *p)
{
  return -1;
}

size_t mpdiff_apply(navit_mapit_t *m2, navit_mpdiff_t *p)
{
  return -1;
}

size_t mpdiff_makediff(int fds0, int fds1, int ifd, int options)
{
  return -1;
}

size_t mpdiff_applydiff(int fds0, int ifd, int options)
{
  return -1;
}
