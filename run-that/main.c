#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "signames.h"

#define PRINT(...) do { \
    fprintf(stderr, "[run-that] " __VA_ARGS__); \
} while (0)

void print_help_and_exit(int exit_code) {
    PRINT("\n\n \
usage: run-that --map [from-sig] [to-sig] --on-start [program / args ..] --program [program / args ..] --after-exit [program / args ..] \n\n \
  --map [from-sig] [to-sig]: this re-maps a signal received by run-that app to the program, you can have more than one mapping\n\n \
  --program [norm program args]: this is the program + it args to be run in the docker\n\n \
  --on-start [start program args]: the start program runs first, before --program. \n\n \
  --after-exit [exit program args]: the exit program runs after the --program as exited. \n\n \
examples: \n\n \
  ./run-that --on-start echo hello --program sleep 2 --after-exit echo goodbye \n\n \
  ./run-that --map TERM QUIT --program /bin/nginx -g daemon off;\n\n \
  ./run-that --map TERM QUIT --on-start wget http://[somesite]/config.json --program /bin/nginx -g daemon off;\n\n");
    exit(exit_code);
}

#define MAX_ARGS 50
static struct {
    char *start_cmd[MAX_ARGS + 1];
    char *program_cmd[MAX_ARGS + 1];
    char *exit_cmd[MAX_ARGS + 1];
    int signal_map[MAX_SIG_NAMES][2];
    int signal_map_len;
} _args;

int map_signal(int signum) {
    int i = 0;
    for (; i < _args.signal_map_len; i++)
        if (_args.signal_map[i][0] == signum)
            return _args.signal_map[i][1];
    return signum;
}

void parse_args(int argc, char** argv) {

    if (argc == 1) {
      print_help_and_exit(1);
    }

    enum {
        INIT_ARGS,
        GET_MAP_ARG_1,
        GET_MAP_ARG_2,
        GET_START_ARG,
        GET_START_ARG_COUNT,
        GET_PROGRAM_ARG,
        GET_PROGRAM_ARG_COUNT,
        GET_EXIT_ARG,
        GET_EXIT_ARG_COUNT
    } state = INIT_ARGS;

    memset(&_args, 0, sizeof(_args));

    int sig_num = -1;

    char **start_cmd = NULL;
    int start_cmd_n = 0;

    char **program_cmd = NULL;
    int program_cmd_n = 0;

    char **exit_cmd = NULL;
    int exit_cmd_n = 0;

    int i = 1;
    for (; i < argc; i++) {
        if (strcasecmp(argv[i], "--help") == 0
                || strcasecmp(argv[i], "-h") == 0) {
            print_help_and_exit(0);
        }
        else if (strcasecmp(argv[i], "--on-start") == 0) {
            state = GET_START_ARG;
        }
        else if (strcasecmp(argv[i], "--program") == 0) {
            state = GET_PROGRAM_ARG;
        }
        else if (strcasecmp(argv[i], "--after-exit") == 0) {
            state = GET_EXIT_ARG;
        }
        else if (state == INIT_ARGS) {

            if (strcasecmp(argv[i], "--map") == 0) {
                state = GET_MAP_ARG_1;
                if (_args.signal_map_len == MAX_SIG_NAMES) {
                    PRINT("ERROR: to many signals mapped, max: %d\n",
                            MAX_SIG_NAMES);
                    print_help_and_exit(1);
                }
            }
            else if (strcasecmp(argv[i], "--help") == 0
                    || strcasecmp(argv[i], "-h") == 0) {
                print_help_and_exit(0);
            }
            else {
                PRINT("ERROR: invalid arguments\n");
                print_help_and_exit(1);
            }

        } else if (state == GET_MAP_ARG_1) {
            if ((sig_num = sig_from_str(argv[i])) < 1) {
                PRINT("ERROR: invalid from signal\n");
                print_help_and_exit(1);
            }
            _args.signal_map[_args.signal_map_len][0] = sig_num;
            state = GET_MAP_ARG_2;

        } else if (state == GET_MAP_ARG_2) {
            if ((sig_num = sig_from_str(argv[i])) < 1) {
                PRINT("ERROR: invalid to signal\n");
                print_help_and_exit(1);
            }
            _args.signal_map[_args.signal_map_len++][1] = sig_num;
            state = INIT_ARGS;

        } else if (state == GET_START_ARG) {
            start_cmd = &argv[i];
            start_cmd_n++;
            state = GET_START_ARG_COUNT;

        } else if (state == GET_START_ARG_COUNT) {
            start_cmd_n++;

        } else if (state == GET_PROGRAM_ARG) {
            program_cmd = &argv[i];
            program_cmd_n++;
            state = GET_PROGRAM_ARG_COUNT;

        } else if (state == GET_PROGRAM_ARG_COUNT) {
            program_cmd_n++;

        } else if (state == GET_EXIT_ARG) {
            exit_cmd = &argv[i];
            exit_cmd_n++;
            state = GET_EXIT_ARG_COUNT;

        } else if (state == GET_EXIT_ARG_COUNT) {
            exit_cmd_n++;
        }
    }

    if(start_cmd_n) {
        for (i = 0; i < start_cmd_n && i < MAX_ARGS; i++)
            _args.start_cmd[i] = start_cmd[i];
    }

    if(program_cmd_n) {
        for (i = 0; i < program_cmd_n && i < MAX_ARGS; i++)
            _args.program_cmd[i] = program_cmd[i];
    }

    if(exit_cmd_n) {
        for (i = 0; i < exit_cmd_n && i < MAX_ARGS; i++)
            _args.exit_cmd[i] = exit_cmd[i];
    }
}

pid_t spawn_cmd(const char *file,
                    char *const argv[],
                    const sigset_t *all_signals) {

    pid_t child_pid = fork();
    if (child_pid < 0) {
        PRINT("ERROR: Unable to fork %s. Exiting.\n", file);
        return child_pid;
    }
    else if (child_pid == 0) {
        sigprocmask(SIG_UNBLOCK, all_signals, NULL);
        if (setsid() == -1) {
            PRINT(
                "ERROR: Unable to setsid (errno=%d %s) for %s. Exiting.\n",
                errno,
                strerror(errno),
                file
            );
            exit(1);
        }
        execvp(file, argv);
        // if this point is reached, exec failed, so we should exit nonzero
        PRINT("ERROR: %s: %s\n", file, strerror(errno));
        return 2;
    }
    return child_pid;
}

int execute_cmd(char **argv) {
     pid_t  pid;
     int    status;

     if ((pid = fork()) < 0) {     /* fork a child process           */
          printf("*** ERROR: forking child process failed\n");
          exit(1);
     }
     else if (pid == 0) {
          if (execvp(*argv, argv) < 0)
               exit(1);
     }
     else {
          while (wait(&status) != pid);
     }
     return status;
}

int main(int argc, char** argv) {

    parse_args(argc, argv);

    if (_args.start_cmd[0] && execute_cmd(_args.start_cmd) != 0) {
        PRINT("ERROR: calling --on-start cmd '%s' failed. Exiting.\n",
              _args.start_cmd[0]);
        exit(2);
    }

    pid_t program_pid = -1;
    pid_t program_exit_status = 0;
    pid_t program_alive = false;

    sigset_t all_signals;
    sigfillset(&all_signals);
    sigprocmask(SIG_BLOCK, &all_signals, NULL);

    program_pid = spawn_cmd(_args.program_cmd[0], _args.program_cmd, &all_signals);
    if (program_pid < 0)
        return program_pid;
    program_alive = true;

    while (program_alive) {
        int signum;
        sigwait(&all_signals, &signum);
        PRINT("Received signal(%d) '%s'.\n", signum, strsignal(signum));
        if (signum == SIGCHLD) {
            int status, exit_status;
            pid_t killed_pid;
            while ((killed_pid = waitpid(-1, &status, WNOHANG)) > 0) {
                if (WIFEXITED(status)) {
                    exit_status = WEXITSTATUS(status);
                }
                else {
                    assert(WIFSIGNALED(status));
                    exit_status = 128 + WTERMSIG(status);
                }
                if (killed_pid == program_pid) {
                    program_alive = false;
                    program_exit_status = exit_status;
                    PRINT("%s (%d) exited with status %d.\n", _args.program_cmd[0], program_pid, exit_status);
                }
                else {
                    PRINT("%s (%d) exited with status %d.\n", _args.program_cmd[0], killed_pid, exit_status);
                }
            }
        }
        else if (signum == SIGTERM || signum == SIGINT) {
            PRINT("starting graceful shutdown\n");
            if (program_pid > 0 && program_alive)
                kill(program_pid, map_signal(SIGTERM));
        }
        else if (signum == SIGKILL) {
            PRINT("starting hard shutdown\n");
            if (program_pid > 0 && program_alive)
                kill(program_pid, SIGKILL);
        }
        else if (program_pid > 0 && program_alive) {
            PRINT("signalling %s (%d): %s\n",
                    _args.program_cmd[0],
                    program_pid,
                    strsignal(map_signal(signum)));
            kill(program_pid, map_signal(signum));
        }
    }

    if (program_exit_status) {
        PRINT("dirty exit: %s status(%d) of main program \n",
               _args.program_cmd[0],
               program_exit_status);
    } else {
        PRINT("clean exit of main program \n");
    }

    if (_args.exit_cmd[0] && execute_cmd(_args.exit_cmd) != 0) {
        PRINT("ERROR: calling --after-exit cmd '%s'.\n",
              _args.exit_cmd[0]);
        exit(3);
    }

    return program_exit_status;
}
