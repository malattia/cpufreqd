/*
 *  Copyright (C) 2006-2008  Mattia Dongili <malattia@linux.it>
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

#include <sysfs/libsysfs.h>
#include "cpufreqd.h"

struct acpi_configuration {
	int battery_update_interval;
	int watch_ac;
	int watch_battery;
	int watch_event;
	int watch_temperature;
	char acpid_sock_path[MAX_PATH_LEN];
};

int read_value(struct sysfs_attribute *attr);
int read_int(struct sysfs_attribute *attr, int *value);


void put_attribute(struct sysfs_attribute *attr);
struct sysfs_attribute *get_class_device_attribute(struct sysfs_class_device *clsdev,
		const char *attrname);

void put_class_device(struct sysfs_class_device *clsdev);
int find_class_device(const char *clsname, const char *devtype,
		int (*clsdev_callback)(struct sysfs_class_device *cls));
