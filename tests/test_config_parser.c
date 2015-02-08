#include <stdlib.h>
#include <stdio.h>
#include <check.h>
#include "cpufreqd_plugin.h"
#include "config_parser.c"

Suite * cpufreqd_suite(void);

START_TEST(test_clean_config_line)
{
	char in[256];
	char out[256];

	sprintf(in, "%s", "   simple test1   ");
	sprintf(out, "%s", "simple test1");
	ck_assert_str_eq(clean_config_line(in), out);

	sprintf(in, "%s", "simple test2   ");
	sprintf(out, "%s", "simple test2");
	ck_assert_str_eq(clean_config_line(in), out);

	sprintf(in, "%s", "   simple test3");
	sprintf(out, "%s", "simple test3");
	ck_assert_str_eq(clean_config_line(in), out);
}
END_TEST

START_TEST(test_strip_comments_line)
{
	char in[256];
	char out[256];

	sprintf(in, "%s", "# comment");
	sprintf(out, "%s", "");
	ck_assert_str_eq(strip_comments_line(in), out);

	sprintf(in, "%s", "test2 # comment");
	sprintf(out, "%s", "test2 ");
	ck_assert_str_eq(strip_comments_line(in), out);

	sprintf(in, "%s", "test3# and another # one");
	sprintf(out, "%s", "test3");
	ck_assert_str_eq(strip_comments_line(in), out);
}
END_TEST

/* test suite boilerplate */

Suite * cpufreqd_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("CpuFreqd");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_clean_config_line);
    tcase_add_test(tc_core, test_strip_comments_line);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = cpufreqd_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
