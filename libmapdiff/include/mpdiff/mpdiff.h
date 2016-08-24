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

#ifndef __MPDIFF_MPDIFF_H__
#define __MPDIFF_MPDIFF_H__

struct nmit_ops;

typedef struct __navit_mmapit_type {
  void *data;
  size_t size;
  int flags;
  /* nobody cares */
  struct nmit_ops *ops;
  void *priv;
} navit_mapit_t;

typedef navit_mapit_t navit_mpdiff_t;

struct nmit_ops {
  int (*create)(navit_mapit_t *, void *);
  int (*free)(navit_mapit_t *);
  void* (*readh)(navit_mapit_t *, size_t, off_t, size_t *);
  size_t (*writeh)(navit_mapit_t *, size_t, off_t, void *);
};

navit_mapit_t *navit_mapit_cc(struct nmit_ops *, void *);
int navit_mapit_free(navit_mapit_t *);

size_t mpdiff_cmp(navit_mapit_t *m1, navit_mapit_t *m2, navit_mpdiff_t *p);

size_t mpdiff_apply(navit_mapit_t *m2, navit_mpdiff_t *p);

/* please note that i'm trying to keep API as simple as possible */

/* this is stupid simple API usefuk for tests and tools
 * in both: -1 returns means error, and errno set to the
 * proper error.
 */
/* compare maps, write a diff, returns a diff size */
size_t mpdiff_makediff(int fds0, int fds1, int ifd, int options);

/* apply a patch to the opened map, returns the size of changes applied */
size_t mpdiff_applydiff(int fds0, int ifd, int options);

#endif /* __MPDIFF_MPDIFF_H__ */
