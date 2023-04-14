/* Program that simulates a mini-shell, with a CLI similar to well-known shells (such as bash)
* Program Functionality: 
* Input
* Word Splitting
* Expansion
* Parsing
* Execution
* Waiting
*/

#define _POSIX_C_SOURCE 200809L
#include "smallsh.h"

// Declare constants and global variables
#define MIN_ARGS    512  // minimum of 512 words supported
int bg_pids[100];  // array to store background PID's
int bg_pidc = 0;  // count of background PID's
int bg_flag = 0;  // flag for background process
int SIGTSTP_flag = 0;  // flag for STGTSTP  
pid_t spawnPid = -5;
pid_t bg_pid = 0;  // store the most recent background pid

// exit status for foreground + background processes
int exit_stat = 0;
int stat_code = 0; 

// variables to check for file redirection
int input_redir = 0;
int output_redir = 0; 
char *input_file;
char *output_file;
char *dollar_exclam = "";  // initialize $! to empty string 
char *tok_copy = NULL;
char *token = NULL;

// Set up signal handling structs
struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, ignore_action = {0}; 
struct sigaction SIGINT_action_old = {0}, SIGTSTP_action_old = {0}; 

// Function Declarations
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub); 
int exec_builtin(char **command_tok);  
int fork_with_redir(char **command_tok, int bg_flag); 
pid_t fork_bg_process(char **command_tok); 

void handle_SIGINT(int signo); 

// Empty function handler for SIGINT
void handle_SIGINT(int signo) {
    // Empty signal handler that does nothing
    // Used for when reading a line of input
}

int main(int argc, char *argv[]) {
    char *lineptr = NULL; 
    char *line_words = NULL;
    size_t buffer_size = 0;
    const char *ps1 = getenv("PS1");
    const char *ifs = getenv("IFS"); 
    const char *path = getenv("PATH"); 
    const char *home_env = getenv("HOME");

    char *token; 
    char *slash = "/";
    const char *delim; 

    // token strings that needs expansion or further processing
    char *needle_home = "~/";
    char *needle_pid = "$$"; 
    char *needle_exitstat = "$?"; 
    char *needle_bgproc = "$!"; 
    char *ampersand = "&"; 
    char *comment = "#";

    // Fill out signal handling structs, set disposition to SIG_IGN
    SIGTSTP_action.sa_handler = SIG_IGN; 
    sigfillset(&SIGTSTP_action.sa_mask); 
    SIGTSTP_action.sa_flags = 0; 

    // Fill out SIGINT_action struct, set initial disposition to SIG_IGN
    SIGINT_action.sa_handler = SIG_IGN;
    // Block all signals, reset flags
    sigfillset(&SIGINT_action.sa_mask); 
    SIGINT_action.sa_flags = 0; 

    // set ignore_action as SIG_IGN as its signal handler
    ignore_action.sa_handler = SIG_IGN; 

    // Register the functions so that SIGINT will be ignored
    sigaction(SIGINT, &ignore_action, NULL);  // initially set to ignore
    sigaction(SIGTSTP, &ignore_action, NULL); 

    for (;;) {
        int redirect_flag = 0; 
        
        if (ifs == NULL) {
            delim = "\t\n"; 
        } else delim = ifs; 
        // get pid of process running in the background 
        pid_t wait_pid = waitpid(0, &exit_stat, WNOHANG | WUNTRACED);
    start: 
        while (wait_pid > 0) 
        {
            if (WIFEXITED(exit_stat)) { 
                // fprintf(stderr,"bg_pid from background: %d\n", bg_pid); 
                fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bg_pid, exit_stat);
            } else if (WIFSIGNALED(exit_stat)) {
                bg_pid = spawnPid; 
                fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) wait_pid, exit_stat); 
            } else if (WIFSTOPPED(exit_stat)) {
                bg_pid = spawnPid;
                fprintf(stderr, "Child process %jd done. Continuing.\n", wait_pid, exit_stat);
            }
            wait_pid = waitpid(0, &stat_code, WNOHANG | WUNTRACED); 
        }

        /* INPUT */
        // Print the command prompt by expanding PS1 parameter
        if (ps1 == NULL) {
            fprintf(stderr, "%s", " "); 
        } else fprintf(stderr, "%s", ps1); 

        // Register SIGINT to a dummy function before getline
        SIGINT_action.sa_handler = handle_SIGINT; 

        // Read a line of input from stdin
        // getline returns number of char's written
        ssize_t line_length = getline(&lineptr, &buffer_size, stdin);  
        if (line_length == -1) 
        {  // error handling for getline
            clearerr(stdin); 
            errno = 0; 
            // fprintf(stderr, "error", errno); 
            exit(-1); 
        } 
        sigaction(SIGINT, &SIGINT_action_old, NULL);
        lineptr[strcspn(lineptr, "\n")] = 0; 

        // allocate space to hold the actual strings read in (line_words)
        line_words = calloc(line_length, sizeof(char)); 
        if (line_words == NULL){
            perror("memory allocation error"); 
            return (-1); 
        }

        strcpy(line_words, lineptr);  // includes terminating \0 character

        // handle empty input
        if (strlen(lineptr) == 0) {
            goto start;
        }

        // determine how many tokens there are
        int num_tokens = 0; 
        token = strtok(lineptr, delim); 
        while (token != NULL) {
            num_tokens++; 
            token = strtok(NULL, delim); 
        }
        num_tokens++;

        /* WORD SPLITTING */
        // Tokenize User Input 
        // Code Adapted from (citation): https://stackoverflow.com/questions/37273193/tokenize-user-input-for-execvp-in-c 
        // DEBUG printf("%s\n", lineptr); 
        token = strtok(line_words, delim);
        char **command_tok = calloc(num_tokens, sizeof(char*)); 
        int i = 0; 
        while (token) {
            // printf("Original Token: %s\n", token); 
            tok_copy = strdup(token);  // copy each word as we tokenize (since strtok breaks down the string)
            // printf("Copy of token: %s\n", tok_copy); 
            // ignore the token if it is a comment
            if (strncmp(&tok_copy[0], comment, 1) == 0) {
                command_tok[i++] = NULL; 
                goto fork_process;
            }
            
            /* EXPANSION of words */
            // if "~/" was found (replace with HOME env variable)
            if (strncmp(tok_copy, needle_home, 2) == 0)  // ~/ can only be found at beginning of word
            {  
            // concatenate char pointers source citation: https://stackoverflow.com/questions/55096198/how-to-concatenate-char-pointers-using-strcat-in-c  
            char *full_homep = malloc(1 + strlen(slash) + strlen(home_env));  // retain the final slash in HOME variable
            strcpy(full_homep, home_env); 
            strcat(full_homep, slash); 
            //DEBUG: printf("Full home_env: %s\n", full_homep);
                char *ret = str_gsub(&tok_copy, needle_home, full_homep); 
                if (!ret) exit(1); 
                tok_copy = ret; 
            }

            // if "$$" was found (replace with process ID of smallsh)
            else if (strstr(tok_copy, needle_pid) != NULL)  // strstr returns NULL pointer if substring not found
            {  
                // pid_t conversion to string
                // Adapted from (Citation): https://stackoverflow.com/questions/15262315/how-to-convert-pid-t-to-string 
                pid_t pid = getpid(); 
                // DEBUG: printf("smallsh pid = %d\n", pid); 
                char smallsh_pid[10]; 
                sprintf(smallsh_pid, "%d", pid); 
                char *ret = str_gsub(&tok_copy, needle_pid, smallsh_pid); 
                if (!ret) exit(1);
                tok_copy = ret; 
            }

            // if "$?" was found (replace with exit status of last foreground command)
            if (strstr(tok_copy, needle_exitstat) != NULL)
            {
                char *child_exit_str = calloc(2048, sizeof(char)); 
                // fprintf(stderr, "Previous Exit Status = %d\n", stat_code); 
                if (stat_code != 0) {  // if waited-for command terminated with exit status
                    // stat_code defaults to 0
                    // DEBUG: printf("exit value: %d\n", exit_stat);
                    sprintf(child_exit_str, "%d", stat_code); 
                    {
                        char *ret = str_gsub(&tok_copy, needle_exitstat, child_exit_str); 
                        if (!ret) exit(1); 
                        tok_copy = ret;
                    }
                }
                else if (stat_code == 0) {
                    sprintf(child_exit_str, "%d", stat_code);
                    {
                        char *ret = str_gsub(&tok_copy, needle_exitstat, child_exit_str); 
                        if (!ret) exit(1); 
                        tok_copy = ret; 
                    }
                }
                else if (WIFEXITED(stat_code)) {
                    // stat_code defaults to 0
                    int exitStat = WEXITSTATUS(stat_code); 
                    // DEBUG: printf("exit value: %d\n", exit_stat);
                    sprintf(child_exit_str, "%d", exitStat); 
                    {
                        char *ret = str_gsub(&tok_copy, needle_exitstat, child_exit_str); 
                        if (!ret) exit(1); 
                        tok_copy = ret;
                    }
                }
                else if (WIFSIGNALED(exit_stat)) {  // if waited-for command terminated due to signal
                    int signal_stat = 128 + (WTERMSIG(exit_stat)); 
                    sprintf(child_exit_str, "%d", signal_stat); 
                    {
                        char *ret = str_gsub(&tok_copy, needle_exitstat, child_exit_str); 
                        if (!ret) exit(1); 
                        tok_copy = ret; 
                    }
                }
            }

            // if "$!" was found (replace with process ID of most recent background process in the same group ID as smallsh)
            if (strstr(tok_copy, needle_bgproc) != NULL) 
            {
                if (bg_pid == 0) 
                {
                    // fprintf(stderr, "bg pid = %d\n", bg_pid); 
                    char *ret = str_gsub(&tok_copy, needle_bgproc, dollar_exclam); 
                    if (!ret) exit(1);
                    tok_copy = ret;     
                }
                else 
                {
                    if (WIFEXITED(exit_stat)) 
                    { 
                    char bg_pid_str[1024]; 
                    sprintf(bg_pid_str, "%d", bg_pid); 
                    char *ret = str_gsub(&tok_copy, needle_bgproc, bg_pid_str); 
                    if (!ret) exit(1); 
                    tok_copy = ret; 
                    }
                }
            }

            command_tok[i++] = tok_copy;
            token = strtok(NULL, delim);  // get the next token 
        }
        command_tok[i] = NULL;

        // execute builtin's after tokenizing
        int builtin_result = exec_builtin(command_tok); 
        if (builtin_result == 1) {  // 1 indicates that no built in command was found
        } else { goto start; }

        /* PARSING: check if redirection or background process is needed */
        for (int i = 0; i < num_tokens-2; i++) 
        {

            if (strncmp(command_tok[i], "<", 1) == 0)
            {
                input_redir = 1;
                command_tok[i] = NULL;
                // fprintf(stderr, "Input File: %s\n", command_tok[i+1]); 
                input_file = command_tok[i+1]; 
            }
            else if (strncmp(command_tok[i], ">", 1) == 0)
            {
                output_redir = 1; 
                command_tok[i] = NULL;
                // fprintf(stderr, "Output File: %s\n", command_tok[i+1]); 
                output_file = command_tok[i+1]; 
            }
        }

        // check if process is to run in the background (if "&" found at the end)
        int bg_process = strncmp(command_tok[num_tokens-2], ampersand, 1); 
        if (strncmp(command_tok[num_tokens-2], ampersand, 1) == 0)
        {
            // fprintf(stderr, "Background process started\n"); 
            command_tok[num_tokens-2] = NULL;
            // set background flag and proceed to fork process
            bg_flag = 1; 
            goto fork_process;
            // pid_t gpid = getpgid(spawnPid);
            // fprintf(stderr, "Group Process ID = %d\n", gpid);
            spawnPid = -5;
        }

        // fprintf(stderr, "Input File = %s, Output File = %s\n", input_file, output_file); 
        if ((input_redir == 1 || output_redir == 1) && (bg_process == 0)) {
            bg_flag = 1; 
            int redir_res = fork_with_redir(command_tok, bg_flag);
            // fprintf(stderr, "Finished redirecting\n"); 
            if (redir_res == 0) // redirecting process done
            {
                input_redir = 0;
                output_redir = 0;
                input_file = NULL;
                output_file = NULL;
                goto start; 
            }
        } 
        else if ((input_redir == 1 || output_redir == 1) && (bg_process != 0)) {
            bg_flag = 0;
            int redir_res = fork_with_redir(command_tok, bg_flag);
            // fprintf(stderr, "Finished redirecting\n"); 
            if (redir_res == 0) // redirecting process done
            {
                input_redir = 0;
                output_redir = 0;
                input_file = NULL;
                output_file = NULL;
                goto start; 
            }
        }
        else {  // no redirecting needed, go straight to fork process
            goto fork_process; 
        }

        /* EXECUTE: Execute non-builtin commands with input and output redirection. */

    // else execute non-builtin commands in new child process 
    // in foreground - with blocking wait
    fork_process:
    {
        spawnPid = fork();
        switch(spawnPid) {
            case -1:
                perror("fork() failed");
                exit(1);
                break;
            case 0:  // child process executes this branch
                // DEBUG: fprintf(stderr, "Command: %s\n", command_tok[0]); 
                sigaction(SIGINT, &SIGINT_action_old, NULL); 
                sigaction(SIGTSTP, &SIGTSTP_action_old, NULL);
                execvp(command_tok[0], command_tok); 
                // execvp only returns on error
                fprintf(stderr, "execvp failed\n");
            default:  // parent process waits for foreground process to finish
                if (bg_flag == 1)  // if background flag is set, process is to run without blocking wait
                {
                    bg_pid = spawnPid;
                    bg_flag = 0;
                    goto start; 
                }
                // waitPid is process ID for the child process
                waitpid(spawnPid, &exit_stat, 0); 
                if (WIFEXITED(exit_stat)) {
                    stat_code = WEXITSTATUS(exit_stat); 
                    // DEBUG: fprintf(stderr, "Exit Status From Process: %d\n", stat_code);
                }
                if (WIFSIGNALED(exit_stat)) {
                    stat_code = WTERMSIG(exit_stat); 
                    // DEBUG: fprintf(stderr, "Exit Signal from Process: %d\n", stat_code); 
                }
                if (WIFSTOPPED(exit_stat)) {
                    // send SIGCONT signal
                    kill(spawnPid, SIGCONT); 
                    // print to stderr
                    fprintf(stderr, "Child process %d stopped. Continuing...\n", spawnPid); 
                }
        }
    }
    // both parent and child execute this
        // TODO: function to execute non-builtin commands
    free(command_tok); 
    free(tok_copy); 
    }
exit:
    return 0; 
}

/* Function to find a needle substring in a haystack string and replace with sub. 
* Returns the final string with the replacement. 
*/
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub) {
    char *str = *haystack;
    size_t haystack_len = strlen(str); 
    size_t const needle_len = strlen(needle),
                sub_len = strlen(sub); 

    for (; (str = strstr(str, needle)); ) {
        ptrdiff_t off = str - *haystack; 
        if (sub_len > needle_len) {
            str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
            if (!str) goto exit; 
            *haystack = str;
            str = *haystack + off; 
        }
        memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len); 
        memcpy(str, sub, sub_len);
        haystack_len = haystack_len + sub_len - needle_len; 
        str += sub_len;  
    }
    str = *haystack; 
    if (sub_len < needle_len) {
        str = realloc(*haystack, sizeof **haystack * (haystack_len + 1)); 
        if (!str) goto exit; 
        *haystack = str; 
    } 

exit:
    return str; 
}

/* Function to handle builtin functions - exit and cd 
* Returns 1 if no builtin commands found, 0 if builtin commands found and executed
*/ 
int exec_builtin(char **command_tok) 
{
    // handle builtin command "exit"
    char *home_env = getenv("HOME");
    if (strncmp(command_tok[0], "exit", 4) == 0) {
        if (command_tok[1] == NULL) {
            fprintf(stderr, "\nexit\n"); 
            // send SIGINT signal to child processes
            for (int j = 0; j < bg_pidc; j++) {
                kill(bg_pids[j], SIGINT); 
            }
            // exit with specified value
            exit(exit_stat); 
        }
        // error if more than one argument is provided or if argument is not integer
        // if (command_tok[2] != NULL || (isdigit(command_tok[1]) == 0)) {
        //     fprintf(stderr, "Error: Incorrect amount or type of argument\n");
        //     exit(1);
        //     break;
        // }
        else {  
            // else print to  stderr
            exit_stat = atoi(command_tok[1]);
            fprintf(stderr, "\nexit\n");
            // send SIGINT signal to child processes
            for (int j = 0; j < bg_pidc; j++) {
                kill(bg_pids[j], SIGINT); 
            }
            // exit with specified value
            exit(exit_stat);
        }
    }

    // handle builtin command cd
    else if (strncmp(command_tok[0], "cd", 2) == 0) 
    {
        // if we have specified path, change to that path
        if (command_tok[1] != NULL) {
            int change_res = chdir(command_tok[1]); 
            // error handling for chdir
            if (change_res == -1) {
                fprintf(stderr, "%s does not exist\n", command_tok[1]);
                exit(1);
            } 
        }
        // return error if there are more than one path specified
        // else if (command_tok[2] != NULL) {
        //     fprintf(stderr, "Error: Too many arguments\n");
        //     exit(1);  
        // } 
        else {  // default to HOME if no path specified
            chdir(home_env); 
        }
    }
    else {
        return 1; 
    }
    return 0; 
}

/*
* Function to fork with redirection while background process is not specified. 
* Returns 0 if function was successful, 1 otherwise. 
*/
int fork_with_redir(char **command_tok, int bg_flag) 
{
    // there is input or output redirecting but not a background process
    int sourceFD = 0;
    int targetFD = 1;
    int result = 1; 

    // input or output redirect specified but not a background process
    spawnPid = fork(); 

    // both input & output specified
    if (input_redir == 1 && output_redir == 1) {
        // check state of fork
        switch (spawnPid) {
            case -1:
                perror("fork() failed\n"); 
                exit(1); 
            case 0:  // execute in child branch
                // TODO: reset all signals to their original dispositions
                // open source file and redirect source
                sourceFD = open(input_file, O_RDONLY); 
                if (sourceFD == -1) {
                    perror("source open() failed\n");
                    exit(1); 
                }

                // redirect stdin to source file
                result = dup2(sourceFD, 0);
                if (result == -1) {
                    perror("source dup2() failed\n");
                    exit(2); 
                }

                // open target file and redirect
                targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0777); 
                if (targetFD == -1) {
                    perror("target open() failed\n"); 
                    exit(1); 
                }
                
                // redirect stdout to target file
                result = dup2(targetFD, 1);
                if (result == -1) {
                    perror("target dup2() failed\n"); 
                    exit(2); 
                }
                // close file and run exec
                fcntl(targetFD, F_SETFD, FD_CLOEXEC); 
                execlp(command_tok[0], command_tok[0], command_tok[1], command_tok[2], command_tok[3], NULL);
                exit(1);
            default:  // in parent process, check if background process 
                if (bg_flag == 1) {
                    bg_pid = spawnPid; 
                    bg_flag = 0; 
                }
                else {
                    waitpid(spawnPid, &exit_stat, 0); 
                    if (WIFEXITED(exit_stat)) {
                        stat_code = WEXITSTATUS(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Status From Process: %d\n", stat_code);
                    }
                    if (WIFSIGNALED(exit_stat)) {
                        stat_code = WTERMSIG(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Signal from Process: %d\n", stat_code); 
                    }
                    if (WIFSTOPPED(exit_stat)) {
                        // send SIGCONT signal
                        kill(spawnPid, SIGCONT); 
                        // print to stderr
                        fprintf(stderr, "Child process %d stopped. Continuing...\n", spawnPid); 
                    }
                }
        }
    }
    // only input redirection specified
    else if (input_redir == 1 && output_redir == 0) 
    {
        switch(spawnPid) {
            case -1:  // catch fork error
                perror("fork() failed"); 
                exit(1); 
            case 0:   // in child process
                // TODO: reset all signals to their original disposition
                // open and redirect source file
                sourceFD = open(input_file, O_RDONLY); 
                if (sourceFD == -1) {
                    perror("open source() failed\n"); 
                    exit(1); 
                }
                
                // redirect stdin to sourceFD
                result = dup2(sourceFD, 0); 
                if (result == -1) {
                    perror("source dup2() failed\n"); 
                    exit(2); 
                }
                
                // close file and exec
                fcntl(sourceFD, F_SETFD, FD_CLOEXEC); 
                execlp(command_tok[0], command_tok[0], command_tok[1], command_tok[2], command_tok[3], NULL); 
                exit(1);
            default:  // in parent process
                if (bg_flag == 1)  // if bg_flag is set
                {
                    bg_pid = spawnPid;
                    bg_flag = 0; 
                }
                    waitpid(spawnPid, &exit_stat, 0); 
                    if (WIFEXITED(exit_stat)) {
                        stat_code = WEXITSTATUS(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Status From Process: %d\n", stat_code);
                    }
                    if (WIFSIGNALED(exit_stat)) {
                        stat_code = WTERMSIG(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Signal from Process: %d\n", stat_code); 
                    }
                    if (WIFSTOPPED(exit_stat)) {
                        // send SIGCONT signal
                        kill(spawnPid, SIGCONT); 
                        // print to stderr
                        fprintf(stderr, "Child process %d stopped. Continuing...\n", spawnPid); 
                    }
        }
    }
    // only output redirection specified
    else if (output_redir == 1 && input_redir == 0) {
        switch (spawnPid) {
            case -1:  // handle fork error
                perror("fork() failed\n");
                exit(1); 
            case 0:  // in child process
                // TODO: reset all signals to their original disposition
                // open and redirect source file
                targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0777); 
                if (targetFD == -1) {
                    perror("open target() failed\n"); 
                    exit(1); 
                }
                
                // redirect stdin to sourceFD
                result = dup2(targetFD, 1); 
                if (result == -1) {
                    perror("target dup2() failed\n"); 
                    exit(2); 
                }
                
                // close file and exec
                fcntl(targetFD, F_SETFD, FD_CLOEXEC); 
                execlp(command_tok[0], command_tok[0], command_tok[1], command_tok[2], command_tok[3], NULL);
                // execvp(command_tok[0], command_tok); 
                exit(1);  
            default:  // in the parent process, perform a non-blocking wait
                if (bg_flag == 1)  // if bg_flag is set
                {
                    bg_pid = spawnPid; 
                    bg_flag = 0; 
                }
                else {
                    waitpid(spawnPid, &exit_stat, 0); 
                    if (WIFEXITED(exit_stat)) {
                        stat_code = WEXITSTATUS(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Status From Process: %d\n", stat_code);
                    }
                    if (WIFSIGNALED(exit_stat)) {
                        stat_code = WTERMSIG(exit_stat); 
                        // DEBUG: fprintf(stderr, "Exit Signal from Process: %d\n", stat_code); 
                    }
                    if (WIFSTOPPED(exit_stat)) {
                        // send SIGCONT signal
                        kill(spawnPid, SIGCONT); 
                        // print to stderr
                        fprintf(stderr, "Child process %d stopped. Continuing...\n", spawnPid); 
                    }
                }
        } 
    }
    else return 1;
    // execvp(command_tok[0], command_tok); 
    // waitpid(spawnPid, &exit_stat, 0);
    return 0; 
}

/* 
* Function to fork and perform background process (with non-blocking wait)
*/
pid_t fork_bg_process(char **command_tok) {
    spawnPid = fork();
    switch(spawnPid) {
        case -1:  // error handling for fork
            perror("fork() failed");
            exit(1);
        case 0:  // execute in child process
            execvp(command_tok[0], command_tok); 
            exit(1); 
        default:  // in parent process perform non-blocking wait
            bg_pids[bg_pidc] = spawnPid; 
            bg_pidc++; 
            // fprintf(stderr, "spawnPid=%d\n", spawnPid);
            bg_pid = spawnPid; 
            // fprintf(stderr, "bg_pid in bg process = %d\n", bg_pid);
    }
    return bg_pid; 
}
