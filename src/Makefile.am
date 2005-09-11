AM_CFLAGS = -W -Wall -Wshadow -W -Wpointer-arith \
	-Wcast-qual -Wcast-align -Wwrite-strings \
	-Wconversion -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wredundant-decls \
	-Wnested-externs -Winline -Wundef -Wbad-function-cast \
	-Waggregate-return \
	-std=c99 \
	-D_POSIX_SOURCE -D_GNU_SOURCE \
	@ADD_CFLAGS@

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

cpufreqd_LDFLAGS = -export-dynamic

BUILD_PLUGINS = \
		cpufreqd_programs.la \
		cpufreqd_cpu.la

if ACPI_PLUGIN
BUILD_PLUGINS += cpufreqd_acpi_battery.la \
		 cpufreqd_acpi_temperature.la \
		 cpufreqd_acpi_ac.la
endif
if NFORCE2_PLUGIN
BUILD_PLUGINS += cpufreqd_nforce2.la
endif
if APM_PLUGIN
BUILD_PLUGINS += cpufreqd_apm.la
endif
if PMU_PLUGIN
BUILD_PLUGINS += cpufreqd_pmu.la
endif
if SENSORS_PLUGIN
BUILD_PLUGINS += cpufreqd_sensors.la
endif
lib_LTLIBRARIES = $(BUILD_PLUGINS)

if NFORCE2_PLUGIN
cpufreqd_nforce2_la_SOURCES = \
		cpufreqd_nforce2.c

cpufreqd_nforce2_la_LDFLAGS = \
		-module -avoid-version
endif

if PMU_PLUGIN
cpufreqd_pmu_la_SOURCES = \
		cpufreqd_pmu.c

cpufreqd_pmu_la_LDFLAGS = \
		-module -avoid-version
endif

if APM_PLUGIN
cpufreqd_apm_la_SOURCES = \
		cpufreqd_apm.c

cpufreqd_apm_la_LDFLAGS = \
		-module -avoid-version
endif

if ACPI_PLUGIN
cpufreqd_acpi_ac_la_SOURCES = \
		cpufreqd_acpi_ac.c

cpufreqd_acpi_ac_la_LDFLAGS = \
		-module -avoid-version
								
cpufreqd_acpi_temperature_la_SOURCES = \
		cpufreqd_acpi_temperature.c

cpufreqd_acpi_temperature_la_LDFLAGS = \
		-module -avoid-version

cpufreqd_acpi_battery_la_SOURCES = \
		cpufreqd_acpi_battery.c

cpufreqd_acpi_battery_la_LDFLAGS = \
		-module -avoid-version
endif

cpufreqd_cpu_la_SOURCES = \
		cpufreqd_cpu.c

cpufreqd_cpu_la_LDFLAGS = \
		-module -avoid-version

cpufreqd_programs_la_SOURCES = \
		cpufreqd_programs.c

cpufreqd_programs_la_LDFLAGS = \
		-module -avoid-version

if SENSORS_PLUGIN
cpufreqd_sensors_la_SOURCES = \
		cpufreqd_sensors.c

cpufreqd_sensors_la_LDFLAGS = \
		-module -avoid-version @ADD_LDFLAGS@
endif

noinst_HEADERS = \
		cpufreqd.h \
		cpufreqd_plugin.h \
		cpufreqd_log.h \
		cpufreq_utils.h \
		daemon_utils.h \
		plugin_utils.h \
		cpufreqd_remote.h \
		sock_utils.h \
		config_parser.h \
		list.h

