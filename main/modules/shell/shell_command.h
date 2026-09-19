#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int stats_command(int argc, char **argv);
int diag_command(int argc, char **argv);
int set_command(int argc, char **argv);
int get_command(int argc, char **argv);
int monitor_command(int argc, char **argv);
int help_command(int argc, char **argv);
int clear_command(int argc, char **argv);

#ifdef __cplusplus
}
#endif
