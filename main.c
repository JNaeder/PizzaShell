#define POSIX_C_SOURCE 200809L
#define GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

// Function definitions
size_t wordsplit(char const *line);
char * expand(char *word);
int run_command(char *words[], int nwords);
int builtin_cd(char *words[]);
int builtin_exit(char *words[]);

int last_fprocess = 0;
int last_bprocess = 0;

char *words[MAX_WORDS] = {NULL};

void handle_SIGINT(int signo){
    char* nl = "\n";
    write(STDERR_FILENO, nl, 1);
}

struct sigaction default_SIGINT;
struct sigaction default_SIGTSTP;


extern char *logo_header;
extern char *title_text;
extern char *help_menu;
char* pizza_toppings[20];

int main(int argc, char *argv[])
{
    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2) {
        input_fn = argv[1];
        input = fopen(input_fn, "re");
        if (!input) err(1, "%s", input_fn);
    } else if (argc > 2) {
        errx(1, "too many arguments");
    }

    char *line = NULL;
    size_t n = 0;
    srand(time(NULL));

    // Get Original signal states
    sigaction(SIGINT, NULL, &default_SIGINT);
    sigaction(SIGTSTP, NULL, &default_SIGTSTP);

//    printf("%s", logo_header);
    printf("%s", title_text);


    for (;;) {

        // Manage Background processes
        int bg_status;
        pid_t bg_term_pid;
        bg_term_pid = waitpid(-1, &bg_status, WNOHANG | WUNTRACED);

        if (bg_term_pid > 0){
            if (WIFEXITED(bg_status)){
                fprintf(stderr, "Child process %d done. Exit status %d.\n", bg_term_pid, WEXITSTATUS(bg_status));
            } else if(WIFSIGNALED(bg_status)) {
                fprintf(stderr, "Child process %d done. Signaled %d.\n", bg_term_pid, WTERMSIG(bg_status));
            } else if(WIFSTOPPED(bg_status)){
                kill(bg_term_pid, SIGCONT);
                fprintf(stderr, "Child process %d stopped. Continuing.\n", bg_term_pid);
            }
        }


        ssize_t line_len;


        if (input == stdin) {
            // This is interactive.
            //Print prompt
            fprintf(stderr, "(> ");

            // Ignore SIGTSTP Signal
            signal(SIGTSTP, SIG_IGN);

            struct sigaction SIGINT_action = {0};
            SIGINT_action.sa_handler = handle_SIGINT;
            sigfillset(&SIGINT_action.sa_mask);
            SIGINT_action.sa_flags = 0;
            sigaction(SIGINT, &SIGINT_action, NULL);

            clearerr(input);

            // Get a line from the input
            line_len = getline(&line, &n, input);
            signal(SIGINT, SIG_IGN);
        } else {
            // This is not
            line_len = getline(&line, &n, input);
        }


        // Check of eof
        if (feof(input)){
            return 0;
        }


        // Read input
        if (line_len > 1){

            // Split the words into the array
            size_t nwords = wordsplit(line);

            // Expand special words into their values
            for (size_t i = 0; i < nwords; ++i) {
                // printf("Word[%d] -> %s\n", i, words[i]);
                char *exp_word = expand(words[i]);
                free(words[i]);
                words[i] = exp_word;
            }

            // Process commands
            if (strcmp(words[0], "exit") == 0){
                builtin_exit(words);
            } else if (strcmp(words[0], "cd") == 0) {
                builtin_cd(words);
            } else if (strcmp(words[0], "help") == 0){
                printf("%s", help_menu);
            }
            else if (strcmp(words[0], "pizza") == 0){
                printf("%s", logo_header);
            }
            else if (strcmp(words[0], "topping") == 0){
                int size = sizeof(pizza_toppings) / sizeof(pizza_toppings[0]);
                printf("%s\n", pizza_toppings[rand() % size]);
            }
            else run_command(words, nwords);



            // Reset Words Array
            for (int i=0; i < MAX_WORDS; i++){
                free(words[i]);
                words[i] = NULL;
            }

        }

    }
}

int builtin_cd(char *words[]){

    // Set the path variable
    char* the_path;
    if (words[1] == NULL){
        // Set path to home path
        the_path = getenv("HOME");
    } else if (words[2] != NULL){
        errx(1, "CD: Too many arguments");
    } else {
        the_path = words[1];
    }

    // Change Directory
    int chdir_result = chdir(the_path);
    if(chdir_result != 0){
        perror("cd");
    }
}


int builtin_exit(char *words[]){
    int value = last_fprocess;
    if (words[2] != NULL)errx(1, "Exit: Too many arguments");
    if (words[1] != NULL) {
        value = atoi(words[1]);
        if (value == 0) errx(1, "Exit: Not a valid number");
    }
    exit(value);
}

int run_command(char *words[], int nwords){
    int run_bground = 0;
    char *arguments[MAX_WORDS] = {NULL};
    int arg_count = 0;
    for(int i = 0; i < nwords; i++){
        char* currword = words[i];

        if (strcmp(currword, "&") == 0){
            run_bground = 1;
            nwords--;
        }
    }

    pid_t child_pid = fork();

    if(child_pid == -1) {
        errx(1, "fork");
    } else if(child_pid == 0){
        // Set signals back to orginal dispositions (oldact / sigaction)
        // signal(SIGSTOP, SIG_DFL); <- not this
        // for (int signum = 1; signum < NSIG; ++signum){
        //   if(is_interactive == 1 && signum == SIGTSTP) continue;
        //   sigaction(signum, &old_sigact, NULL);
        // }

        sigaction(SIGINT, &default_SIGINT, NULL);
        sigaction(SIGTSTP, &default_SIGTSTP, NULL);


        for(int i = 0; i < nwords; i++){
            char* currword = words[i];

            if (strcmp(currword, ">") == 0){
                // Open or create a file for overwriting, and duplicate the file descriptor for stdout
                int fd = open(words[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
                if (fd == -1) errx(1, "Error opening %s", words[i + 1]);
                int dup_fd = dup2(fd, 1);
                if (dup_fd == -1) errx(1, "Error duplicating fd");
                i++;

            } else if (strcmp(currword, "<") == 0){
                // Open the file and duplicate the file descriptor for stdin
                int fd = open(words[i + 1], O_RDONLY);
                if (fd == -1) errx(1, "Error opening %s", words[i + 1]);
                int dup_fd = dup2(fd, 0);
                if (dup_fd == -1) errx(1, "Error duplicating fd");
                i++;

            } else if (strcmp(currword, ">>") == 0){
                // Open or create a file for appending and duplicate the file descriptor for stdout
                int fd = open(words[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0777);
                if (fd == -1) errx(1, "Error opening %s", words[i + 1]);
                int dup_fd = dup2(fd, 1);
                if (dup_fd == -1) errx(1, "Error duplicating fd");
                i++;
            } else {
                arguments[arg_count] = currword;
                arg_count++;
            }
        }

        // Check if command is found
        execvp(words[0], arguments);
        errx(1, "Cannot find command: %s", words[0]);

    } else {
        // This is the parent process
        int status;
        pid_t term_cpid;
        if(run_bground){
            // Run in background
            term_cpid = waitpid(child_pid, &status, WNOHANG);
            last_bprocess = child_pid;
        } else {
            // Run in foreground, wait for child process to complete
            term_cpid = waitpid(child_pid, &status, WUNTRACED);

            if(WIFEXITED(status)){
                last_fprocess = WEXITSTATUS(status);
            } else if(WIFSIGNALED(status)){
                last_fprocess = WTERMSIG(status) + 128;
            } else if(WIFSTOPPED(status)){
                kill(term_cpid, SIGCONT);
                last_bprocess = term_cpid;
                fprintf(stderr, "Child process %d stopped. Continuing.\n", term_cpid);
            }
            if(term_cpid == -1)errx(1, "term_cpid");
        }
        return 0;

    }

}



/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (;*c && isspace(*c); ++c); /* discard leading space */

    for (; *c;) {
        if (wind == MAX_WORDS) break;
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


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char *word, char **start, char **end)
{
    static char *prev;
    if (!word) word = prev;

    char ret = 0;
    *start = 0;
    *end = 0;
    for (char *s = word; *s && !ret; ++s) {
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

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
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

/* Expands all instances of $! $$ $? and ${param} in a string
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char *word)
{
    char *pos = word;
    char *start, *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);

    while (c) {
        // printf("While loop c -> %c start -> %c\n", c, start);
        if (c == '!'){
            // Get PID of recent background processs (waiting)
            char statusStr[20];
            sprintf(statusStr, "%d", last_bprocess);
            if (last_bprocess == 0){
                build_str("", NULL);
            } else {
                build_str(statusStr, NULL);
            }
        }
        else if (c == '$'){
            // Get PID
            pid_t pid = getpid();
            char pidStr[20];
            sprintf(pidStr, "%d", pid);
            build_str(pidStr, NULL);
        }
        else if (c == '?'){
            // Get Exit status of last foreground command
            char statusStr[20];
            sprintf(statusStr, "%d", last_fprocess);
            build_str(statusStr, NULL);
        }
        else if (c == '{') {
            // Get Parameter

            const char* p_start = strchr(start, '{');
            const char* p_end = strchr(start, '}');

            int param_length = p_end - p_start - 1;
            char param_value[param_length + 1];
            strncpy(param_value, p_start + 1, param_length);
            param_value[param_length] = '\0';
            // printf("Parameter value -> %s\n", param_value);
            char* env_var = getenv(param_value);
            if (env_var == NULL) env_var = "";
            build_str(env_var, NULL);
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        // printf("End of while loop c -> %c start -> %c\n", c, start);
        build_str(pos, start);
    }

    return build_str(start, NULL);
}

