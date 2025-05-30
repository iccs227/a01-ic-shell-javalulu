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
#include <errno.h>

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n"
#define MAX_JOBS 32

// Job status enum
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobStatus;

// Job structure
typedef struct {
    int id;             // Job ID (1-based)
    pid_t pid;          // Process ID
    JobStatus status;   // Current status
    char *command;      // Command string
} Job;

// Global job list
Job *jobs[MAX_JOBS] = {NULL};
int next_job_id = 1;

// Global variables
static volatile pid_t fg_pid = 0;        // foreground process ID
static int last_exit = 0;                // last command exit status
static int script_mode = 0;              // script mode flag
char *last_line = NULL;                  // for '!!' history

// Function declarations
char **split_line(char *line);
void free_tokens(char **tokens);
void add_job(pid_t pid, const char *cmd);
void remove_job(int job_id);
void update_job_status(void);
void list_jobs(void);
Job *find_job_by_id(int job_id);
void job_done_handler(int sig, siginfo_t *info, void *context);

// SIGINT handler to kill foreground job
void handle_sigint(int sig) {
    pid_t local_fg_pid = fg_pid;  // Read volatile variable once
    if (local_fg_pid > 0) {
        kill(local_fg_pid, SIGINT);
    }
}

// SIGTSTP handler to suspend foreground job
void handle_sigtstp(int sig) {
    pid_t local_fg_pid = fg_pid;  // Read volatile variable once
    if (local_fg_pid > 0) {
        kill(local_fg_pid, SIGTSTP);
    }
}

// Add a new job to the job list
void add_job(pid_t pid, const char *cmd) {
    // Handle job ID wraparound
    if (next_job_id > 9999) {  // Reset after a large number
        next_job_id = 1;
        // Ensure the new ID doesn't conflict with existing jobs
        while (1) {
            int conflict = 0;
            for (int i = 0; i < MAX_JOBS; i++) {
                if (jobs[i] && jobs[i]->id == next_job_id) {
                    conflict = 1;
                    next_job_id++;
                    break;
                }
            }
            if (!conflict) break;
        }
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] == NULL) {
            jobs[i] = malloc(sizeof(Job));
            if (!jobs[i]) {
                perror("malloc");
                return;
            }
            jobs[i]->id = next_job_id++;
            jobs[i]->pid = pid;
            jobs[i]->status = JOB_RUNNING;
            jobs[i]->command = strdup(cmd);
            if (!jobs[i]->command) {
                perror("strdup");
                free(jobs[i]);
                jobs[i] = NULL;
                return;
            }
            return;
        }
    }
    fprintf(stderr, "Maximum number of jobs reached\n");
}

// Remove a job from the job list
void remove_job(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] && jobs[i]->id == job_id) {
            free(jobs[i]->command);
            free(jobs[i]);
            jobs[i] = NULL;
            return;
        }
    }
}

// Update status of all jobs
void update_job_status(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i]) {
            int status;
            pid_t result = waitpid(jobs[i]->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
            
            if (result == -1) {
                if (errno != ECHILD) {
                    perror("waitpid");
                }
                remove_job(jobs[i]->id);
                i--;  // Recheck this index
                continue;
            }
            
            if (result == jobs[i]->pid) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    if (WIFSIGNALED(status)) {
                        printf("[%d]+ Terminated by signal %d\t%s\n", 
                               jobs[i]->id, WTERMSIG(status), jobs[i]->command);
                    } else {
                        printf("[%d]+ Done\t%s\n", jobs[i]->id, jobs[i]->command);
                    }
                    int job_id = jobs[i]->id;
                    remove_job(job_id);
                    i--;  // Recheck this index
                } else if (WIFSTOPPED(status) && jobs[i]->status != JOB_STOPPED) {
                    jobs[i]->status = JOB_STOPPED;
                    printf("[%d]+ Stopped\t%s\n", jobs[i]->id, jobs[i]->command);
                } else if (WIFCONTINUED(status) && jobs[i]->status != JOB_RUNNING) {
                    jobs[i]->status = JOB_RUNNING;
                    printf("[%d]+ Continued\t%s\n", jobs[i]->id, jobs[i]->command);
                }
            }
        }
    }
}

// List all jobs
void list_jobs(void) {
    update_job_status();  // Update status before listing
    int has_jobs = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i]) {
            has_jobs = 1;
            printf("[%d]%c %s\t%s\n", 
                jobs[i]->id,
                (i == MAX_JOBS - 1 || jobs[i+1] == NULL) ? '+' : '-',
                jobs[i]->status == JOB_STOPPED ? "Stopped" : "Running",
                jobs[i]->command);
        }
    }
    if (!has_jobs && !script_mode) {
        // Optional: print "No jobs" message in interactive mode
        // printf("No active jobs\n");
    }
}

// Find a job by its ID
Job *find_job_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] && jobs[i]->id == job_id) {
            return jobs[i];
        }
    }
    return NULL;
}

// Handler for SIGCHLD to catch job completion
void job_done_handler(int sig, siginfo_t *info, void *context) {
    pid_t local_fg_pid = fg_pid;  // Read volatile variable once
    if (info->si_pid != local_fg_pid) {  // Only handle background jobs
        // Save errno as it might be changed by update_job_status
        int saved_errno = errno;
        update_job_status();
        errno = saved_errno;
    }
}

// Add job cleanup function
void cleanup_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i]) {
            // Send SIGTERM to each job
            if (kill(jobs[i]->pid, SIGTERM) == 0) {
                // Wait only if the process exists
                int status;
                waitpid(jobs[i]->pid, &status, 0);
            }
            if (jobs[i]->command) {
                free(jobs[i]->command);
            }
            free(jobs[i]);
            jobs[i] = NULL;
        }
    }
}

int main(int argc, char *argv[]) {
    // Remove atexit registration since we'll handle cleanup directly
    
    // install signal handlers
    struct sigaction sa_int, sa_tstp, sa_chld;
    
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_handler = handle_sigint;
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_handler = handle_sigtstp;
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    // Set up SIGCHLD handler for background job completion
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_sigaction = job_done_handler;
    sa_chld.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    // support reading from script file
    FILE *input = stdin;
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
            update_job_status();  // Update job status before showing prompt
            printf("icsh $ ");
            fflush(stdout);
        }

        linelen = getline(&line, &bufsize, input);
        if (linelen == -1) {
            if (!script_mode) printf("\n");
            break;
        }

        // Ensure we have a newline after any background job completion messages
        if (!script_mode) {
            fflush(stdout);
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
            free_tokens(args);
            continue;
        }

        // parse I/O redirection tokens
        int infile = -1, outfile = -1;    // redirection file descriptors
        int redirect_error = 0;  // Flag for redirection errors
        int out_redirects = 0;   // Count output redirections
        
        for (int i = 0; args[i]; i++) {
            if (strcmp(args[i], "<") == 0) {
                // input redirection
                if (args[i+1]) {
                    if (infile != -1) {
                        fprintf(stderr, "error: multiple input redirections\n");
                        redirect_error = 1;
                        break;
                    }
                    infile = open(args[i+1], O_RDONLY);
                    if (infile < 0) {
                        perror("open");
                        redirect_error = 1;
                        break;
                    }
                } else {
                    fprintf(stderr, "syntax error near unexpected token '<'\n");
                    redirect_error = 1;
                    break;
                }
                // remove '<' and filename from args
                int j = i;
                while (args[j+2]) { args[j] = args[j+2]; j++; }
                args[j] = args[j+1] = NULL;
                i--;
            } else if (strcmp(args[i], ">") == 0) {
                // output redirection
                if (args[i+1]) {
                    out_redirects++;
                    if (out_redirects > 1) {
                        fprintf(stderr, "error: multiple output redirections\n");
                        redirect_error = 1;
                        break;
                    }
                    outfile = open(args[i+1], O_CREAT|O_WRONLY|O_TRUNC, 0644);
                    if (outfile < 0) {
                        perror("open");
                        redirect_error = 1;
                        break;
                    }
                } else {
                    fprintf(stderr, "syntax error near unexpected token '>'\n");
                    redirect_error = 1;
                    break;
                }
                // remove '>' and filename
                int j = i;
                while (args[j+2]) { args[j] = args[j+2]; j++; }
                args[j] = args[j+1] = NULL;
                i--;
            }
        }

        // Clean up if there were redirection errors
        if (redirect_error) {
            if (infile != -1) close(infile);
            if (outfile != -1) close(outfile);
            free_tokens(args);
            last_exit = 1;
            if (!script_mode) {
                printf("\n");  // Add newline after error messages
            }
            continue;
        }

        // built-ins
        if (strcmp(args[0], "exit") == 0) {
            int status = args[1] ? atoi(args[1]) & 0xFF : 0;
            if (!script_mode) printf("bye\n");
            
            // Clean up in a specific order
            free_tokens(args);
            cleanup_jobs();  // Clean up jobs first
            if (line) free(line);
            if (last_line) free(last_line);
            if (script_mode) fclose(input);
            
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
            int saved = -1;
            if (outfile != -1) { saved = dup(STDOUT_FILENO); dup2(outfile, STDOUT_FILENO); }
            for (int i = 1; args[i]; i++) {
                printf("%s", args[i]);
                if (args[i+1]) putchar(' ');
            }
            putchar('\n');
            if (saved != -1) { dup2(saved, STDOUT_FILENO); close(saved); }
            last_exit = 0;
        }
        // Job control commands
        else if (strcmp(args[0], "jobs") == 0) {
            list_jobs();
            last_exit = 0;
        }
        else if (strcmp(args[0], "fg") == 0) {
            if (args[1] && args[1][0] == '%') {
                int job_id = atoi(args[1] + 1);
                Job *job = find_job_by_id(job_id);
                if (job) {
                    fg_pid = job->pid;
                    if (job->status == JOB_STOPPED) {
                        kill(job->pid, SIGCONT);
                    }
                    int status;
                    waitpid(job->pid, &status, WUNTRACED);
                    if (WIFEXITED(status)) {
                        remove_job(job_id);
                        last_exit = WEXITSTATUS(status);
                    } else if (WIFSTOPPED(status)) {
                        job->status = JOB_STOPPED;
                        printf("\n[%d]+ Stopped\t%s\n", job->id, job->command);
                        last_exit = 128 + WSTOPSIG(status);
                    }
                    fg_pid = 0;
                } else {
                    fprintf(stderr, "fg: no such job\n");
                    last_exit = 1;
                }
            } else {
                fprintf(stderr, "fg: invalid job specification\n");
                last_exit = 1;
            }
        }
        else if (strcmp(args[0], "bg") == 0) {
            if (args[1] && args[1][0] == '%') {
                int job_id = atoi(args[1] + 1);
                Job *job = find_job_by_id(job_id);
                if (job) {
                    if (job->status == JOB_STOPPED) {
                        kill(job->pid, SIGCONT);
                        job->status = JOB_RUNNING;
                        printf("[%d]+ Running\t%s &\n", job->id, job->command);
                        last_exit = 0;
                    } else {
                        fprintf(stderr, "bg: job %d is not stopped\n", job_id);
                        last_exit = 1;
                    }
                } else {
                    fprintf(stderr, "bg: no such job\n");
                    last_exit = 1;
                }
            } else {
                fprintf(stderr, "bg: invalid job specification\n");
                last_exit = 1;
            }
        }
        // external command execution
        else {
            // Check if command should run in background
            int is_background = 0;
            char *cmd_str = NULL;
            
            // Remove '&' from args if present
            for (int i = 0; args[i]; i++) {
                if (args[i+1] == NULL && strcmp(args[i], "&") == 0) {
                    is_background = 1;
                    args[i] = NULL;  // Just nullify it, don't free individual token
                    break;
                }
            }

            // Create command string for job list
            if (is_background || !args[0]) {  // Also check if args[0] exists
                size_t total_len = 0;
                for (int i = 0; args[i]; i++) {
                    total_len += strlen(args[i]) + 1;  // +1 for space
                }
                cmd_str = malloc(total_len + 1);  // +1 for null terminator
                if (!cmd_str) {
                    perror("malloc");
                    last_exit = 1;
                    continue;
                }
                cmd_str[0] = '\0';
                for (int i = 0; args[i]; i++) {
                    if (i > 0) strcat(cmd_str, " ");
                    strcat(cmd_str, args[i]);
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                free(cmd_str);
                last_exit = 1;
            } else if (pid == 0) {
                // restore default signals in child
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                // apply redirection in child
                if (infile != -1) {
                    dup2(infile, STDIN_FILENO);
                    close(infile);
                }
                if (outfile != -1) {
                    dup2(outfile, STDOUT_FILENO);
                    close(outfile);
                }

                execvp(args[0], args);
                perror("execvp");
                if (infile != -1) close(infile);
                if (outfile != -1) close(outfile);
                exit(1);
            } else {
                if (is_background) {
                    // Add to job list and print job info
                    add_job(pid, cmd_str);
                    printf("[%d] %d\n", next_job_id - 1, pid);
                    free(cmd_str);  // Free after adding to job list
                    last_exit = 0;
                    // Close file descriptors in parent for background process
                    if (infile != -1) close(infile);
                    if (outfile != -1) close(outfile);
                } else {
                    fg_pid = pid;
                    int wstatus;
                    waitpid(pid, &wstatus, WUNTRACED);
                    // Close file descriptors in parent
                    if (infile != -1) close(infile);
                    if (outfile != -1) close(outfile);
                    if (WIFEXITED(wstatus)) {
                        last_exit = WEXITSTATUS(wstatus);
                    } else if (WIFSIGNALED(wstatus)) {
                        last_exit = 128 + WTERMSIG(wstatus);
                    } else if (WIFSTOPPED(wstatus)) {
                        // Create command string for stopped job
                        size_t total_len = 0;
                        for (int i = 0; args[i]; i++) {
                            total_len += strlen(args[i]) + 1;
                        }
                        cmd_str = malloc(total_len + 1);
                        if (!cmd_str) {
                            perror("malloc");
                            last_exit = 1;
                            continue;
                        }
                        cmd_str[0] = '\0';
                        for (int i = 0; args[i]; i++) {
                            if (i > 0) strcat(cmd_str, " ");
                            strcat(cmd_str, args[i]);
                        }
                        add_job(pid, cmd_str);
                        // Explicitly set the job status to STOPPED
                        for (int i = 0; i < MAX_JOBS; i++) {
                            if (jobs[i] && jobs[i]->pid == pid) {
                                jobs[i]->status = JOB_STOPPED;
                                break;
                            }
                        }
                        printf("\n[%d]+ Stopped\t%s\n", next_job_id - 1, cmd_str);
                        free(cmd_str);
                        last_exit = 128 + WSTOPSIG(wstatus);
                    }
                    fg_pid = 0;
                }
            }
        }

        free_tokens(args);
    }

    free(line);
    free(last_line);
    if (script_mode) fclose(input);
    return 0;
}

// split input into tokens
char **split_line(char *line) {
    int bufsize = TOK_BUFSIZE, pos = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token, *saveptr;
    char *line_copy = strdup(line);  // Create a copy of the line

    if (!tokens || !line_copy) {
        perror("allocation error");
        exit(1);
    }

    token = strtok_r(line_copy, TOK_DELIM, &saveptr);
    while (token) {
        tokens[pos] = strdup(token);  // Make a copy of each token
        pos++;

        if (pos >= bufsize) {
            bufsize *= 2;
            char **new_tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!new_tokens) {
                perror("reallocation error");
                exit(1);
            }
            tokens = new_tokens;
        }
        token = strtok_r(NULL, TOK_DELIM, &saveptr);
    }
    tokens[pos] = NULL;
    free(line_copy);  // Free the copy of the line
    return tokens;
}

// Helper function to free the token array
void free_tokens(char **tokens) {
    if (!tokens) return;
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}