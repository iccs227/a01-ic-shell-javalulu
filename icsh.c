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

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n"

char **split_line(char *line);

// for '!!' history
char *last_line = NULL;

int main(int argc, char *argv[]) {

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
        }
        else if (strcmp(args[0], "help") == 0) {
            printf("ICShell built-in commands：\n");
            printf("  echo <text>\n");
            printf("  !!\n");
            printf("  exit [n]\n");
            printf("  cd [dir]\n");
            printf("  help\n");
        }
        else if (strcmp(args[0], "echo") == 0) {
            for (int i = 1; args[i]; i++) {
                printf("%s", args[i]);
                if (args[i+1]) putchar(' ');
            }
            putchar('\n');
        }

        else {
            pid_t pid = fork();                  // create child process
            if (pid < 0) {
                perror("fork");
            } else if (pid == 0) {
                execvp(args[0], args);           // replace child with program
                perror("execvp");              // exec failed
                exit(1);                         // exit child on failure
            } else {
                int wstatus;
                waitpid(pid, &wstatus, 0);       // wait for child to finish
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