/**
 * CS374 - Operating Systems
 * Joel Strong - stronjoe@oregonstate.edu
 * Assignment 3 - smallsh
 * 11/10/2023
 *
 * Description: Creation of a small shell program for running commands from the command line.  The program does the
 * following:
 * 1. Reads input from the command line OR from a file if a valid CMD line argument file is provided.
 * 2. First word in the command is a built-in executable command (cd or exit) or a linux executable program.
 * 3. Enables input/output redirection -
 * 3a.  Uses < + input file to read input from a file
 * 3b.  Uses > + write file to write (replace) output to a file
 * 3c.  Uses >> + append file to append output to a file
 * 4. If the last word in the command is the & symbol, it will run the process in the background.
 * 5. If no background, symbol, it will perform a blocking wait on the execution of the foreground process.
 * 6. Will monitor the status of all processes and provide outputs for their pid's and exit statuses.
 * 7. Provides the following signal handling:
 * 7a.  Will ignore ALL SIGTSTP signals
 * 7b.  Will ignore ALL SIGINT signals except when reading commands from the command line
 * 7c.  Will reset all signals in each child process
 *
 * The wordsplit, param_scan, build_str, and expand methods were provided by the professor.  It would have been easier
 * to build my own methods, but wanted to demonstrate the ability to read and understand existing code and build
 * functionality on top of someone elses code.
 */

/* FILE DEFINITIONS */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/fcntl.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

struct sh_options {
    pid_t parent_pid, process_pid, background_pid, background_pids[10];
    int exit_status, child_status, index, error, background_count, children, interactive;
    size_t n_words;
    FILE *input;
    struct sigaction sigint_action, sig_ignore, sigint_saved, sigtstp_saved;
};

char *words[MAX_WORDS];
int manage_background (struct sh_options *opts);
int print_prompt (struct sh_options *opts);
size_t wordsplit(char const *line);
char *expand(char const *word, struct sh_options *opts);
int parse_words(struct sh_options *opts);
void exit_pgm(size_t i, struct sh_options *opts);
int change_dir(size_t i, struct sh_options *opts);
void sigint_handler (int sig);
int execute(struct sh_options *opts, char* exec_arr[], char* redir_arr[], int redir_len, int background);



/**
 * Main executable for the program.  Will read lines from stdin or a file input with each command seperated by a newline
 * and each word/symbol seperated by a space.  Will continue executing until the exit command in stdin or until the
 * end of file is reached.
 * @param argc - char* of the program name (shellsh)
 * @param argv - char* of the input file name (OPTIONAL)
 * @return - 0 if executed without error, else exit with error code
 */
int main(int argc, char *argv[]) {
    // init program vars
    struct sh_options *opts = malloc(sizeof(struct sh_options));
    opts->exit_status = 0;        // exit status of last exited process
    opts->parent_pid = getpid();  // parent process pid
    opts->process_pid = -5;       // last child process pid
    opts->background_pid = 0;     // last background process pid
    opts->child_status = -5;      // exit status of the last child process
    opts->interactive = 1;        // indicates if reading from stdin or file
    for (int i = 0; i < 10; i++) {  // array of all background processes
        opts->background_pids[i] = -1;
    }
    char *line = NULL;
    size_t n = 0;

    // get input
    opts->input = stdin;
    if (argc == 2) {
        char *input_fn = argv[1];
        opts->input = fopen(input_fn, "re");  // e flag for cloexec
        if (opts->input == NULL) {
            fprintf(stderr, "Error opening file %s\n", input_fn);
        } else opts->interactive = 0;
    } else if (argc > 2) {
        errx(1, "too many arguments");
    }

    // init signal handlers and set SIGTSTP to ignore
    if (opts->interactive == 1) {
        // ignore signals handler
        opts->sig_ignore.sa_handler = SIG_IGN;
        sigfillset(&opts->sig_ignore.sa_mask);
        opts->sig_ignore.sa_flags = 0;

        // sigint handler
        opts->sigint_action.sa_handler = sigint_handler;
        sigfillset(&opts->sigint_action.sa_mask);
        opts->sigint_action.sa_flags = 0;

        // ignore sigtstp
        sigaction(SIGTSTP, &opts->sig_ignore, &opts->sigtstp_saved);  // save starting state for sigtstp
        sigaction(SIGINT, &opts->sigint_action, &opts->sigint_saved); // save starting state for sigint

    }

    // loop for reading lines from input source
    while (1) {
        start:
        fflush(stdout);
        opts->index = 0;
        opts->n_words = 0;

        // Manage background processes
        manage_background(opts);

        // prompt in interactive mode
        if (opts->interactive == 1) {
            print_prompt(opts);
        }

        // ignore SIGINT signals while reading next line
        if (opts->interactive == 1) sigaction(SIGINT, &opts->sigint_action, NULL);

        // getline from input source
        ssize_t line_len = getline(&line, &n, opts->input);
        if (line_len == -1) {
            if (feof(opts->input)) {
                break;
            } else if (errno == EINTR && opts->interactive == 1) {
                // reset errors if signals interfere with getline
                errno = 0;
                fprintf(stderr, "\n");
                clearerr(stdin);
                opts->error = 1;
            } else {
                //fprintf(stderr, "Error reading from getline\n");
                opts->error = 1;
            }
        }

        // set SIGINT to ignore
        if (opts->interactive == 1) sigaction(SIGINT, &opts->sig_ignore, NULL);

        // restart if error
        if (opts->error == 1) {
            opts->error = 0;
            goto start;
        }

        // split input into words, expand, parse, and execute (within parse)
        opts->n_words = wordsplit(line);
        for (size_t i = 0; i < opts->n_words; ++i) {
            //fprintf(stderr, "Word %zu: %s\n", i, words[i]);
            char *exp_word = expand(words[i], opts);
            free(words[i]);
            words[i] = exp_word;
            //fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
        }
        parse_words(opts);
    }

    // EXIT INPUT LOOP AND CLEANUP - close files, kill processes, free memory
    if (opts->input != stdin) fclose(opts->input);
    int exit_status = opts->exit_status;
    for (int j = 0; j < 10; j++) {
        if (opts->background_pids[j] != -1) {
            kill(opts->background_pids[j], 2);
        }
    }
    //fprintf(stderr, "Child count: %d\n", opts->children);
    free(opts);
    exit(exit_status);
}

/**
 * Empty signal handler for handling signals during command line input
 */
void sigint_handler (int sig) {};

// GLOBAL words array
char *words[MAX_WORDS] = {0};

/**
 * Prints command line prompt during interactive mode
 */
int print_prompt (struct sh_options *opts) {
    if (opts->input == stdin) {
        uid_t user = getuid();
        uid_t euser = getegid();
        char *PSx = getenv("PS1");
        if (PSx != NULL) fprintf(stderr, "%s", PSx);
        else {
            if (user == euser) fprintf(stderr, "%s", "$ ");
            else fprintf(stderr, "%s", "# ");
        }
    }
    return 0;
}

/**
 * Manages the statuses of background processes and prints/resumes their statuses
 */
int manage_background (struct sh_options *opts) {
    /* check background process */

    opts->process_pid = waitpid(0, &opts->child_status, WNOHANG | WUNTRACED);

    /* if process exited */
    if (WIFEXITED(opts->child_status) && opts->process_pid > 0) {
        int exit_status = WEXITSTATUS(opts->child_status);
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) opts->process_pid, exit_status);
    }
    /* process is signaled */
    if (WIFSIGNALED(opts->child_status)  && opts->process_pid > 0)  {
        int signal_num = WTERMSIG(opts->child_status);
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) opts->process_pid, signal_num);
    }
    /* process is stopped */
    if (WIFSTOPPED(opts->child_status) && opts->process_pid > 0) {
        kill(opts->process_pid, SIGCONT);
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) opts->process_pid);
    }
    return 0;
}

/**
 * For EACH command line: execute built-in commands exit or cd, parse other commands into an executable array or a
 * file re-direciton array (re-direction happens within the child process), and determine if command is run in the
 * background. Send the commands for execution.
 */
int parse_words(struct sh_options *opts) {
    int exec_len = 0, redir_len = 0, background = 0, reset = 0;
    char *exec_arr[MAX_WORDS] = {0};
    char *redir_arr[MAX_WORDS] = {0};
    for (int i = 0; i < opts->n_words; i++) {
        /* init new loop vars */
        int next = i <= opts->n_words ? 1 : 0;
        char *word = words[i];

        // exit PARENT process
        if (strcmp("exit", word) == 0) {
            exit_pgm(i, opts);
        }

        // change directories in the PARENT process
        else if (strcmp("cd", word) == 0) {
            change_dir(i, opts);
            i++;
            return 0;

        // detect file redirection commands and write to a redirect array
        } else if ((strcmp("<", word) == 0) || (strcmp(">", word) == 0) || (strcmp(">>", word) == 0)) {
            if (!next) {
                fprintf(stderr, "Invalid file redirect - no file argument.");
                exit(1);
            } else {
                redir_arr[redir_len] = words[i]; // save redirect symbol
                redir_arr[redir_len + 1] = words[i  + 1]; // save redirect file
                redir_len += 2;
                i++;
            }

        // detect if a background command
        } else if (strcmp("&", word) == 0) {
            background = 1;

        // add executable commands to the execute array
        } else {
                exec_arr[exec_len] = word;
                exec_len++;
        }

        // execute at the end of each line
        if (i >= opts->n_words - 1) {
            execute(opts, exec_arr, redir_arr, redir_len, background);
            opts->index = i;
            reset = 1;
        }

        // reset variables after each execution
        if (reset == 1) {
            exec_len = 0, redir_len= 0, background = 0;
            for (int j = 0; j < MAX_WORDS; j++) {
                exec_arr[j] = NULL;
                redir_arr[j] = NULL;
            }
            reset = 0;
        }
    }
    return 0;
}

/**
 * Exit the parent process
 */
void exit_pgm(size_t i, struct sh_options *opts){
    int exit_num;
    /* if no cmd line arg after exit use exit status of last foreground cmd */
    if (i + 1 == opts->n_words) {
        exit(opts->exit_status);
        /* if more than one cmd line arg, exit with error */
    } else if (opts->n_words > i + 2) {
        //fprintf(stderr, "Error: More than one command line argument provided with exit.");
        exit(1);
    } else {
        exit_num = (int) strtol(words[i + 1], NULL, 0);
        //if (exit_num == 0) fprintf(stderr, "Exit argument was not a number");
        exit(exit_num);
    }
}

/**
 * Change directories in the parent process
 */
int change_dir(size_t i, struct sh_options *opts) {
    int success = 0;
    /* if no arguments */
    if (i == opts->n_words - 1) {
        success = chdir(getenv("HOME"));
        /* if one argument */
    } else if (i + 1 == opts->n_words - 1) {
        success = chdir(words[i + 1]);
        /* if more than one argument */
    } else {
        //fprintf(stderr, "Too many change directory arguments provided");
        exit(1);
    }
    if (success != 0) {
        //fprintf(stderr, "Error changing directory");
        exit(1);
    }
    return 0;
}

/**
 * Execute the command line statement in a child process redirecting or running in the background  if requested.
 * @param opts - global data struct for file commands
 * @param exec_arr - executable command array with 0 index holding the command
 * @param redir_arr - array of redirection commands (to be executed in order)
 * @param redir_len - length of the redirection array
 * @param background - if background command is present or not
 */
int execute(struct sh_options *opts, char* exec_arr[], char* redir_arr[], int redir_len, int background) {
    int read_fd = -5;
    int write_fd = -5;
    int append_fd = -5;
    int success = -5;
    pid_t child_pid = -5;

    child_pid = fork();
    if (child_pid != -1) opts->children++;

    // if fork failed
    switch (child_pid) {
        case -1:
            //fprintf(stderr, "Error creating fork");
            exit(1);

        // CHILD PROCESS - reset signals or original state, perform re-direction and execute command
        case 0:

            // reset all signals
            sigaction(SIGINT, &opts->sigint_saved, NULL);
            sigaction(SIGTSTP, &opts->sigtstp_saved, NULL);

            // perform file redirection
            if (redir_arr[0] != NULL) {
                for (int r = 0; r < redir_len; r++) {
                    if (strcmp("<", redir_arr[r]) == 0) {
                        // open for reading
                        read_fd = open(redir_arr[r + 1], O_RDONLY);
                        if (read_fd == -1) {
                            //fprintf(stderr, "Error opening file for reading: %s \n", read);
                            exit(1);
                        }
                        // redirect to stdin
                        success = dup2(read_fd, STDIN_FILENO);
                        if (success == -1) {
                            //fprintf(stderr, "Error redirecting file for reading: %s \n", read);
                            exit(1);
                        }
                        r++;

                    } else if (strcmp(">", redir_arr[r]) == 0) {
                        // open for writing
                        write_fd = open(redir_arr[r + 1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (write_fd == -1) {
                            //fprintf(stderr, "Error opening file for writing %s \n", write);
                            exit(1);
                        }
                        // redirect to stdout
                        success = dup2(write_fd, STDOUT_FILENO);
                        if (success == -1) {
                            //fprintf(stderr, "Error redirecting file for writing %s \n", write);
                            exit(1);
                        }
                        r++;
                        } else if (strcmp(">>", redir_arr[r]) == 0) {
                        // open for appending
                        append_fd = open(redir_arr[r + 1], O_WRONLY | O_APPEND | O_CREAT, 0777);
                        if (append_fd == -1) {
                            //fprintf(stderr, "Error opening file for appending %s \n", append);
                            exit(1);
                        }
                        // redirect to stdout
                        success = dup2(append_fd, STDOUT_FILENO);
                        if (success == -1) {
                            //fprintf(stderr, "Error redirecting file for appending %s \n", append);
                            exit(1);
                        }
                        r++;
                    }
                }
            }

            // execute command in the child process
            if (execvp(exec_arr[0], exec_arr) == -1) {
                //fprintf(stderr, "Error executing command %s in child process\n", exec_arr[0]);
                exit(1);
            }

            // close redirection file descriptors
            if (read_fd != -5) close(read_fd);
            if (write_fd != -5) close(write_fd);
            if (append_fd != -5) close(append_fd);
            break;

        // PARENT PROCESS
        default:
            // FOREGROUND PROCESSES
            if (background == 0) {
                // perform non-blocking wait and set exit value after exit
                opts->process_pid = waitpid(child_pid, &opts->child_status, WUNTRACED);

                if (WIFEXITED(opts->child_status)) {
                    opts->exit_status = WEXITSTATUS(opts->child_status);
                }
                // if signaled - set exit value to n + 128
                if WIFSIGNALED(opts->child_status) {
                    opts->exit_status = WTERMSIG(opts->child_status) + 128;
                }
                // if process has stopped - restart and run in the background
                if WIFSTOPPED(opts->child_status) {
                    kill(child_pid, SIGCONT);
                    fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) child_pid);
                    opts->background_pid = opts->process_pid;
                    opts->background_pids[opts->background_count] = child_pid;
                    opts->background_count++;
                }
            }
            // BACKGROUND PROCESSES
            else {
                opts->background_pid = child_pid;
                opts->background_pids[opts->background_count] = child_pid;
                opts->background_count++;
            }
    }
    fflush(stdout);

    return 0;
}

/**
 * Splits command line entries into words - code from the professor
 */
size_t wordsplit(char const *line) {
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (;*c && isspace(*c); ++c); /* discard leading space */

    for (; *c;) {
        if (wind == 512) break;
        /* read a word */
        if (*c == '#') break;
        for (;*c && !isspace(*c); ++c) {
            if (*c == '\\') ++c;
            void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
            if (!tmp) err(1, "realloc");
            words[wind] = tmp;
            words[wind][wlen++] = *c;
            words[wind][wlen] = '\0';
        }
        ++wind;
        wlen = 0;
        for (;*c && isspace(*c); ++c);
    }
    return wind;
}

/**
 * Detects certain symbols in a word and records their start/end pointers - code from the professor
 */
char param_scan(char const *word, char const **start, char const **end) {
    static char const *prev;
    if (!word) word = prev;

    char ret = 0;
    *start = 0;
    *end = 0;
    for (char const *s = word; *s && !ret; ++s) {
        s = strchr(s, '$');
        if (!s) break;
        switch (s[1]) {
            case '$':
            case '!':
            case '?':
                ret = s[1];
                *start = s;
                *end = s + 2;
                break;
            case '{':;
                char *e = strchr(s + 2, '}');
                if (e) {
                    ret = s[1];
                    *start = s;
                    *end = e + 1;
                }
                break;
        }
    }
    prev = *end;
    return ret;
}

/**
 * String builder method for building up memory allocated strings - code from professor
 */
char *build_str(char const *start, char const *end) {
    static size_t base_len = 0;
    static char *base = 0;

    if (!start) {
        /* Reset; new base string, return old one */
        char *ret = base;
        base = NULL;
        base_len = 0;
        return ret;
    }
    /* Append [start, end) to base string
     * If end is NULL, append whole start string to base string.
     * Returns a newly allocated string that the caller must free.
     */
    size_t n = end ? end - start : strlen(start);
    size_t newsize = sizeof *base *(base_len + n + 1);
    void *tmp = realloc(base, newsize);
    if (!tmp) err(1, "realloc");
    base = tmp;
    memcpy(base + base_len, start, n);
    base_len += n;
    base[base_len] = '\0';

    return base;
}

/**
 * Method for replacing certain symbols with data -- template code from the professor
 */
char *expand(char const *word, struct sh_options *opts) {
    char const *pos = word;
    char const *start, *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);

    while (c) {
        // $! symbol - replace with pid of most recent background process OR "" if none
        if (c == '!') {
            if (opts->background_pid == 0) {
                build_str("", NULL);
            } else {
                char bkg_pid_str[12] = {0};
                sprintf(bkg_pid_str, "%d", opts->background_pid);
                build_str(bkg_pid_str, NULL);
                }
            }
        // $$ symbol - replace with pid of smallsh parent process
        else if (c == '$') {
            char pid_str[12] = {0};
            sprintf(pid_str, "%d", opts->parent_pid);
            build_str(pid_str, NULL);
        // $? symbol - replace with the exit status of last foreground process OR "0" if none
        } else if (c == '?') {
            char status_str[12] = {0};
            sprintf(status_str, "%d", opts->exit_status);
            build_str(status_str, NULL);
        // ${PARAM} - replace PARAM with the environment variable of the process or "" if not valid
        } else if (c == '{') {
            char envvar[MAX_WORDS] = {0};
            int env_len = end - start - 3;
            strncpy(envvar, start + 2, env_len);
            char* getvar = getenv(envvar);
            if (getvar == NULL) {
                build_str("", NULL);
            } else {
                build_str(getvar, NULL);
            }
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        build_str(pos, start);
    }
    return build_str(start, NULL);
}
