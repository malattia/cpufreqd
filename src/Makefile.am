AM_CFLAGS = -W -Wall -Wshadow -W -Wpointer-arith \
	-Wcast-qual -Wcast-align -Wwrite-strings \
	-Wconversion -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wredundant-decls \
	-Wnested-externs -Winline -Wundef -Wbad-function-cast \
	-Waggregate-return \
	-std=c99 -pedantic \
	-D_POSIX_SOURCE -D_GNU_SOURCE

AM_LDFLAGS = -module -avoid-version -export-dynamic

sbin_PROGRAMS = cpufreqd

cpufreqd_SOURCES = \
		main.c \
		config_parser.c \
		cpufreqd_log.c \
		daemon_utils.c \
		plugin_utils.c \
		sock_utils.c \
		cpufreq_utils.c \
		list.c

lib_LTLIBRARIES = \
		cpufreqd_programs.la \
		cpufreqd_acpi_battery.la \
		cpufreqd_acpi_temperature.la \
		cpufreqd_acpi_ac.la \
		cpufreqd_nforce2.la \
		cpufreqd_apm.la \
		cpufreqd_cpu.la

cpufreqd_nforce2_la_SOURCES = \
		cpufreqd_nforce2.c

cpufreqd_apm_la_SOURCES = \
		cpufreqd_apm.c

cpufreqd_acpi_ac_la_SOURCES = \
		cpufreqd_acpi_ac.c
								
cpufreqd_acpi_temperature_la_SOURCES = \
		cpufreqd_acpi_temperature.c

cpufreqd_acpi_battery_la_SOURCES = \
		cpufreqd_acpi_battery.c

cpufreqd_cpu_la_SOURCES = \
		cpufreqd_cpu.c

cpufreqd_programs_la_SOURCES = \
		cpufreqd_programs.c

noinst_HEADERS = \
		cpufreqd.h \
		cpufreqd_plugin.h \
		cpufreqd_log.h \
		cpufreq_utils.h \
		daemon_utils.h \
		plugin_utils.h \
		cpufreqd_remote.h \
		sock_utils.h \
		main.h \
		config_parser.h \
		list.h

