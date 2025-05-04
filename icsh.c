/* ICCS227: Project 1: icsh
 * Name: Enze Yu
 * StudentID: 6580537
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n"

char **split_line(char *line);

char *last_line = NULL;

int main(int argc, char *argv[]) {
    char *line = NULL;
    size_t bufsize = 0;
    ssize_t linelen;

    while (1) {
        printf("icsh $ ");
        fflush(stdout);

        linelen = getline(&line, &bufsize, stdin);
        if (linelen == -1) {
            printf("\n");
            break;
        }

        if (line[linelen-1] == '\n') {
            line[linelen-1] = '\0';
            linelen--;
        }

        if (strcmp(line, "!!") == 0) {
            if (last_line) {
                printf("%s\n", last_line);
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

        if (strcmp(args[0], "exit") == 0) {
            int status = 0;
            if (args[1]) {
                status = atoi(args[1]) & 0xFF;
            }
            printf("bye\n");
            free(args);
            free(line);
            free(last_line);
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
            printf("Bad Command\n");
        }

        free(args);
    }

    free(line);
    free(last_line);
    return 0;
}

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