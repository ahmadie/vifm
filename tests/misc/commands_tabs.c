#include <stic.h>

#include <stddef.h> /* NULL */
#include <stdio.h> /* snprintf() */
#include <string.h> /* strcpy() */

#include "../../src/cfg/config.h"
#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"
#include "../../src/modes/view.h"
#include "../../src/modes/wk.h"
#include "../../src/ui/tabs.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/fs.h"
#include "../../src/cmd_core.h"
#include "../../src/compare.h"
#include "../../src/filelist.h"

#include "utils.h"

SETUP()
{
	view_setup(&lwin);
	setup_grid(&lwin, 1, 1, 1);
	curr_view = &lwin;
	view_setup(&rwin);
	setup_grid(&rwin, 1, 1, 1);
	other_view = &rwin;

	init_modes();

	opt_handlers_setup();

	columns_setup_column(SK_BY_NAME);
	columns_setup_column(SK_BY_SIZE);

	init_commands();
}

TEARDOWN()
{
	vle_cmds_reset();

	tabs_only(&lwin);
	tabs_only(&rwin);
	cfg.pane_tabs = 0;
	tabs_only(&lwin);

	vle_keys_reset();

	opt_handlers_teardown();

	view_teardown(&lwin);
	view_teardown(&rwin);

	columns_teardown();
}

TEST(tab_is_created_without_name)
{
	tab_info_t tab_info;

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));

	assert_true(tabs_get(&lwin, 1, &tab_info));
	assert_string_equal(NULL, tab_info.name);
}

TEST(tab_is_not_created_on_wrong_path)
{
	char cwd[PATH_MAX + 1];
	assert_non_null(get_cwd(cwd, sizeof(cwd)));
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "", cwd);

	(void)exec_commands("tabnew no-such-subdir", &lwin, CIT_COMMAND);
	assert_int_equal(1, tabs_count(&lwin));

	tab_info_t tab_info;
	assert_true(tabs_get(&lwin, 0, &tab_info));
	assert_true(paths_are_same(lwin.curr_dir, TEST_DATA_PATH));
}

TEST(tab_in_path_is_created)
{
	char cwd[PATH_MAX + 1];
	assert_non_null(get_cwd(cwd, sizeof(cwd)));

	char test_data[PATH_MAX + 1];
	make_abs_path(test_data, sizeof(test_data), TEST_DATA_PATH, "", cwd);

	strcpy(lwin.curr_dir, test_data);

	assert_success(exec_commands("tabnew read", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));

	tab_info_t tab_info;
	assert_true(tabs_get(&lwin, 1, &tab_info));

	char read_data[PATH_MAX + 1];
	snprintf(read_data, sizeof(read_data), "%s/read", test_data);
	assert_true(paths_are_same(lwin.curr_dir, read_data));
}

TEST(tab_in_parent_is_created)
{
	char cwd[PATH_MAX + 1];
	assert_non_null(get_cwd(cwd, sizeof(cwd)));

	char test_data[PATH_MAX + 1];
	make_abs_path(test_data, sizeof(test_data), TEST_DATA_PATH, "", cwd);

	snprintf(lwin.curr_dir, sizeof(lwin.curr_dir), "%s/read", test_data);

	assert_success(exec_commands("tabnew ..", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));

	tab_info_t tab_info;
	assert_true(tabs_get(&lwin, 1, &tab_info));

	assert_true(paths_are_same(lwin.curr_dir, test_data));
}

TEST(newtab_fails_in_diff_mode_for_tab_panes)
{
	create_file(SANDBOX_PATH "/empty");

	strcpy(lwin.curr_dir, SANDBOX_PATH);
	strcpy(rwin.curr_dir, SANDBOX_PATH);

	cfg.pane_tabs = 1;
	(void)compare_two_panes(CT_CONTENTS, LT_ALL, 1, 0);
	assert_failure(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));

	assert_success(remove(SANDBOX_PATH "/empty"));
}

TEST(tab_name_is_set)
{
	tab_info_t tab_info;

	assert_success(exec_commands("tabname new-name", &lwin, CIT_COMMAND));

	assert_true(tabs_get(&lwin, 0, &tab_info));
	assert_string_equal("new-name", tab_info.name);
}

TEST(tab_name_is_reset)
{
	tab_info_t tab_info;

	assert_success(exec_commands("tabname new-name", &lwin, CIT_COMMAND));
	assert_success(exec_commands("tabname", &lwin, CIT_COMMAND));

	assert_true(tabs_get(&lwin, 0, &tab_info));
	assert_string_equal(NULL, tab_info.name);
}

TEST(tab_is_closed)
{
	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_success(exec_commands("tabclose", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));
}

TEST(last_tab_is_not_closed)
{
	assert_success(exec_commands("tabclose", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));
}

TEST(quit_commands_close_tabs)
{
	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_success(exec_commands("quit", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_success(exec_commands("wq", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	(void)vle_keys_exec_timed_out(WK_Z WK_Z);
	assert_int_equal(1, tabs_count(&lwin));

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	(void)vle_keys_exec_timed_out(WK_Z WK_Q);
	assert_int_equal(1, tabs_count(&lwin));
}

TEST(quit_all_commands_ignore_tabs)
{
	extern int vifm_tests_exited;

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));

	vifm_tests_exited = 0;
	assert_success(exec_commands("qall", &lwin, CIT_COMMAND));
	assert_true(vifm_tests_exited);

	vifm_tests_exited = 0;
	assert_success(exec_commands("wqall", &lwin, CIT_COMMAND));
	assert_true(vifm_tests_exited);

	vifm_tests_exited = 0;
	assert_success(exec_commands("xall", &lwin, CIT_COMMAND));
	assert_true(vifm_tests_exited);

	assert_int_equal(2, tabs_count(&lwin));
}

TEST(tabs_are_switched_with_shortcuts)
{
	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));

	(void)vle_keys_exec_timed_out(WK_g WK_t);
	assert_int_equal(0, tabs_current(&lwin));

	(void)vle_keys_exec_timed_out(WK_g WK_T);
	assert_int_equal(1, tabs_current(&lwin));

	(void)vle_keys_exec_timed_out(L"1" WK_g WK_t);
	assert_int_equal(0, tabs_current(&lwin));
}

TEST(tabs_are_switched_with_commands)
{
	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));

	/* Valid arguments. */

	assert_success(exec_commands("tabnext", &lwin, CIT_COMMAND));
	assert_int_equal(0, tabs_current(&lwin));

	assert_success(exec_commands("tabnext", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_success(exec_commands("tabnext 1", &lwin, CIT_COMMAND));
	assert_int_equal(0, tabs_current(&lwin));

	assert_success(exec_commands("tabnext 1", &lwin, CIT_COMMAND));
	assert_int_equal(0, tabs_current(&lwin));

	assert_success(exec_commands("tabnext 2", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_current(&lwin));

	assert_success(exec_commands("tabprevious", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_success(exec_commands("tabprevious 2", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_current(&lwin));

	assert_success(exec_commands("tabprevious 3", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_current(&lwin));

	assert_success(exec_commands("tabprevious 4", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	/* Invalid arguments. */

	assert_failure(exec_commands("tabnext 1z", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabnext -1", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabnext 4", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabnext 10", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabprevious 0", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabprevious -1", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));

	assert_failure(exec_commands("tabprevious -1", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_current(&lwin));
}

TEST(tabs_are_moved)
{
	for (cfg.pane_tabs = 0; cfg.pane_tabs < 2; ++cfg.pane_tabs)
	{
		assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
		assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));

		assert_int_equal(2, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 0", &lwin, CIT_COMMAND));
		assert_int_equal(0, tabs_current(&lwin));
		assert_success(exec_commands("tabmove 1", &lwin, CIT_COMMAND));
		assert_int_equal(0, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 2", &lwin, CIT_COMMAND));
		assert_int_equal(1, tabs_current(&lwin));
		assert_success(exec_commands("tabmove 2", &lwin, CIT_COMMAND));
		assert_int_equal(1, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 3", &lwin, CIT_COMMAND));
		assert_int_equal(2, tabs_current(&lwin));
		assert_success(exec_commands("tabmove 3", &lwin, CIT_COMMAND));
		assert_int_equal(2, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 1", &lwin, CIT_COMMAND));
		assert_int_equal(1, tabs_current(&lwin));
		assert_success(exec_commands("tabmove", &lwin, CIT_COMMAND));
		assert_int_equal(2, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 0", &lwin, CIT_COMMAND));
		assert_int_equal(0, tabs_current(&lwin));
		assert_success(exec_commands("tabmove $", &lwin, CIT_COMMAND));
		assert_int_equal(2, tabs_current(&lwin));

		assert_success(exec_commands("tabmove 0", &lwin, CIT_COMMAND));
		assert_int_equal(0, tabs_current(&lwin));
		assert_failure(exec_commands("tabmove wrong", &lwin, CIT_COMMAND));
		assert_int_equal(0, tabs_current(&lwin));

		tabs_only(&lwin);
	}
}

TEST(view_mode_is_fine_with_tabs)
{
	char cwd[PATH_MAX + 1];
	assert_non_null(get_cwd(cwd, sizeof(cwd)));
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "read",
			cwd);
	populate_dir_list(&lwin, 0);

	(void)vle_keys_exec_timed_out(WK_e);
	(void)vle_keys_exec_timed_out(WK_q);

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));

	(void)vle_keys_exec_timed_out(WK_e);
	(void)vle_keys_exec_timed_out(WK_q);

	assert_success(exec_commands("tabclose", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));

	(void)vle_keys_exec_timed_out(WK_e);
	(void)vle_keys_exec_timed_out(WK_q);
}

TEST(left_view_mode_is_fine_with_tabs)
{
	char cwd[PATH_MAX + 1];
	assert_non_null(get_cwd(cwd, sizeof(cwd)));
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "read",
			cwd);
	populate_dir_list(&lwin, 0);

	(void)vle_keys_exec_timed_out(WK_e);
	(void)vle_keys_exec_timed_out(WK_C_i);

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));

	(void)vle_keys_exec_timed_out(WK_SPACE);
	(void)vle_keys_exec_timed_out(WK_e);
	(void)vle_keys_exec_timed_out(WK_C_i);

	assert_success(exec_commands("tabnew", &lwin, CIT_COMMAND));
	assert_int_equal(3, tabs_count(&lwin));

	assert_success(exec_commands("q", &lwin, CIT_COMMAND));
	assert_int_equal(2, tabs_count(&lwin));
	assert_success(exec_commands("q", &lwin, CIT_COMMAND));
	assert_int_equal(1, tabs_count(&lwin));

	(void)vle_keys_exec_timed_out(WK_SPACE);
	(void)vle_keys_exec_timed_out(WK_q);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */