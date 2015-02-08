AM_CFLAGS = -g -W -Wall -Wshadow -W -Wpointer-arith \
	-Wcast-qual -Wcast-align -Wwrite-strings \
	-Wconversion -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wredundant-decls \
	-Wnested-externs -Winline -Wundef -Wbad-function-cast \
	-Waggregate-return \
	-std=c99 \
	-D_POSIX_SOURCE -D_GNU_SOURCE \
	-I${top_builddir}/src \
	@CHECK_CFLAGS@

LDADD = @CHECK_LIBS@ ${CPUFREQD_LIBS}
LDFLAGS = -export-dynamic @CPUFREQD_LDFLAGS@

CPUFREQD_LIBS = \
	  ${top_builddir}/src/cpufreqd_log.o \
	  ${top_builddir}/src/daemon_utils.o \
	  ${top_builddir}/src/plugin_utils.o \
	  ${top_builddir}/src/sock_utils.o \
	  ${top_builddir}/src/cpufreq_utils.o \
	  ${top_builddir}/src/list.o

TESTS = test_config_parser
check_PROGRAMS = test_config_parser

#test_config_parser_SOURCES = test_config_parser.c
