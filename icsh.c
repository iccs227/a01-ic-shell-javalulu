/* ICCS227: Project 1: icsh
 * Name: Enze Yu
 * StudentID: 6580537
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h> 
#include <fcntl.h>

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n"

char **split_line(char *line);

// for '!!' history
char *last_line = NULL;

static pid_t fg_pid = 0;        // foreground process ID
static int last_exit = 0;        // last command exit status

// SIGINT handler to kill foreground job
void handle_sigint(int sig) {
    if (fg_pid > 0) kill(fg_pid, SIGINT);
}

// SIGTSTP handler to suspend foreground job
void handle_sigtstp(int sig) {
    if (fg_pid > 0) kill(fg_pid, SIGTSTP);
}

int main(int argc, char *argv[]) {
    // install signal handlers
    struct sigaction sa_int, sa_tstp;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_handler = handle_sigint;
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_handler = handle_sigtstp;
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    // support reading from script file
    FILE *input = stdin;
    int script_mode = 0;
    if (argc > 1) {
        input = fopen(argv[1], "r"); // open script file
        if (!input) {
            perror("fopen");
            exit(1);
        }
        script_mode = 1; // flag script mode
    }

    char *line = NULL;
    size_t bufsize = 0;
    ssize_t linelen;

    while (1) {
        if (!script_mode) {
            printf("icsh $ ");
            fflush(stdout);
        }

        linelen = getline(&line, &bufsize, input);
        if (linelen == -1) {
            if (!script_mode) printf("\n");
            break;
        }

        if (line[linelen-1] == '\n') {
            line[linelen-1] = '\0';
            linelen--;
        }

        // history: '!!'
        if (strcmp(line, "!!") == 0) {
            if (last_line) {
                // echo history only in interactive mode
                if (!script_mode) printf("%s\n", last_line);
                free(line);
                line = strdup(last_line);
                linelen = strlen(line);
            } else {
                continue;
            }
        } else if (linelen > 0) {
            free(last_line);
            last_line = strdup(line);
        }

        char **args = split_line(line);
        if (args[0] == NULL) {
            free(args);
            continue;
        }

        // parse I/O redirection tokens
        int infile = -1, outfile = -1;    // redirection file descriptors
        for (int i = 0; args[i]; i++) {
            if (strcmp(args[i], "<") == 0) {
                // input redirection
                if (args[i+1]) {
                    infile = open(args[i+1], O_RDONLY);
                    if (infile < 0) perror("open");
                }
                // remove '<' and filename from args
                int j = i;
                while (args[j+2]) { args[j] = args[j+2]; j++; }
                args[j] = args[j+1] = NULL;
                i--;
            } else if (strcmp(args[i], ">") == 0) {
                // output redirection
                if (args[i+1]) {
                    outfile = open(args[i+1], O_CREAT|O_WRONLY|O_TRUNC, 0644);
                    if (outfile < 0) perror("open");
                }
                // remove '>' and filename
                int j = i;
                while (args[j+2]) { args[j] = args[j+2]; j++; }
                args[j] = args[j+1] = NULL;
                i--;
            }
        }

        // built-ins
        if (strcmp(args[0], "exit") == 0) {
            int status = args[1] ? atoi(args[1]) & 0xFF : 0;
            //suppress goodbye in script mode
            if (!script_mode) printf("bye\n");
            free(args);
            free(line);
            free(last_line);
            if (script_mode) fclose(input); // close script file
            exit(status);
        }
        else if (strcmp(args[0], "cd") == 0) {
            char *dir = args[1] ? args[1] : getenv("HOME");
            if (chdir(dir) != 0) {
                perror("cd");
            }
            last_exit = 0;
        }
        else if (strcmp(args[0], "help") == 0) {
            printf("ICShell built-in commands：\n");
            printf("  echo <text>\n");
            printf("  echo $?\n");
            printf("  !!\n");
            printf("  exit [n]\n");
            printf("  cd [dir]\n");
            printf("  help\n");
            last_exit = 0;
        }
        else if (strcmp(args[0], "echo") == 0 && args[1] && strcmp(args[1], "$?") == 0 && args[2] == NULL) {
            printf("%d\n", last_exit);
            last_exit = 0;
        }
        else if (strcmp(args[0], "echo") == 0) {
            int saved = -1;                                            // save STDOUT
            if (outfile != -1) { saved = dup(STDOUT_FILENO); dup2(outfile, STDOUT_FILENO); }
            for (int i = 1; args[i]; i++) {
                printf("%s", args[i]);
                if (args[i+1]) putchar(' ');
            }
            putchar('\n');
            if (saved != -1) { dup2(saved, STDOUT_FILENO); close(saved); } // restore STDOUT
            last_exit = 0;
        }
        // external command execution
        else {
            pid_t pid = fork();                  // create child process
            if (pid < 0) {
                perror("fork");
                last_exit = 1;
            } else if (pid == 0) {
                // restore default signals in child
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                // apply redirection in child
                if (infile != -1) {
                    dup2(infile, STDIN_FILENO);
                    close(infile);
                }
                if (outfile != -1) {
                    dup2(outfile, STDOUT_FILENO);
                    close(outfile);
                }

                execvp(args[0], args);           // replace child with program
                perror("execvp");              // exec failed
                exit(1);                         // exit child on failure
            } else {
                fg_pid = pid;  // track fg PID
                int wstatus;
                waitpid(pid, &wstatus, WUNTRACED);       // wait for child to finish
                // close fds in parent
                if (infile != -1) close(infile);
                if (outfile != -1) close(outfile);
                if (WIFEXITED(wstatus)) last_exit = WEXITSTATUS(wstatus);
                else if (WIFSIGNALED(wstatus)) last_exit = 128 + WTERMSIG(wstatus);
                else if (WIFSTOPPED(wstatus)) last_exit = 128 + WSTOPSIG(wstatus);
                fg_pid = 0;
            }
        }

        free(args);
    }

    free(line);
    free(last_line);
    // ensure file closed if in script mode
    if (script_mode) fclose(input);
    return 0;
}

// split input into tokens
char **split_line(char *line) {
    int bufsize = TOK_BUFSIZE, pos = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token = strtok(line, TOK_DELIM);

    while (token) {
        tokens[pos++] = token;
        if (pos >= bufsize) {
            bufsize *= 2;
            tokens = realloc(tokens, bufsize * sizeof(char*));
        }
        token = strtok(NULL, TOK_DELIM);
    }
    tokens[pos] = NULL;
    return tokens;
}