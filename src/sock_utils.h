/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@linux.it>
 *                           George Staikos <staikos@0wned.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/stat.h>

#define CPUFREQD_SOCKET		"/cpufreqd"
#define TMP_DIR_TEMPL		"/tmp/cpufreqd-XXXXXX"
#define TMP_DIR_TEMPL_LEN	21

char *create_temp_dir(char *buf, gid_t gid);
void delete_temp_dir(const char *name);
int open_unix_sock(const char *dirname, gid_t gid);
void close_unix_sock(int fd);
