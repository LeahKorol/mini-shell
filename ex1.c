/*
an updated shell
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ENV_VARS 100
#define MAX_INPUT_LENGTH 510
#define MAX_ARGS 10
#define MAX_COMMANDS 10

#define SPACE " "
#define SPACE_CHAR ' '

#define SYSTEM_FAILURES 2
#define INVALID_INPUT -1
#define SUCCESS 1
#define EXIT 3

void print_prompt(char *, char *, int, int);

char *read_command();

void split_multiple_commands(char **);

int split_single_command(char **, char *command, int *);

int execute_single_command(char **, int, char **);

int execute_pipe_commands(char *);

void make_fork(pid_t *p);

int make_exec(char **args);

int wait_to_child(pid_t p, int run_in_background);

void remove_spaces_and_quotes(char *);

void remove_quotes(char *);

int is_assignment(char *, char *);

int deal_with_echo(char *command, char *argv[12], int *args_index);

void divide_string(char *str, char **first, char **second, char **third, int *arg_index);

int count_arguments(char **arr, int n);

void catch_child(int);

void catch_stop(int);

//functions that manages env_vars[]
int my_setenv(char *, char *);

char *my_getenv(char *);

void free_env_vars();

//The struct represents an environment variable: name & value
struct env_var {
    char *name;
    char *value;
};

int cmd_count = 0, arg_count = 0, decreased_counters = 0; //need to be global, so  catch_child() will be able to change them
pid_t run_now, stopped_process;

//this is a data structure to maintain environment variables:
//an array of structs, where each struct represents an environment variable
struct env_var env_vars[MAX_ENV_VARS];
int env_var_count = 0;

//here the program actually runs
int main() {
    char prompt[512], cwd[512]; //current working directory
    char *command;
    int enter_count = 0;

    signal(SIGCHLD, catch_child);
    signal(SIGTSTP, catch_stop);

    while (1) {
        print_prompt(prompt, cwd, sizeof(prompt), sizeof(cwd));
        command = read_command();
        if (command == NULL)
            continue;

        if ((command[0]) != '\n') {
            split_multiple_commands(&command);
            enter_count = 0;
        } else { //the user pressed 'enter' only, increase enter_count
            enter_count++;
        }
        free(command);//dynamically allocated by getline()
        if (enter_count == 3) { //The user pressed enter 3 times consecutively - exit
            free_env_vars();
            exit(0);
        }
    }
    return 0;
}

void print_prompt(char *prompt, char *cwd, int p_size, int c_size) {
    if (getcwd(cwd, c_size) == NULL) { // Get current working directory
        perror("getcwd() error");
        exit(1);
    }
    // Generate the prompt string
    snprintf(prompt, p_size, "#cmd:%d|#args:%d @%s ", cmd_count, arg_count, cwd);
    printf("%s", prompt);
    fflush(stdout);
}

char *read_command() { //get the command from the user
    char *command;
    size_t size = 0;
    ssize_t command_len;//might be negative
    command_len = getline(&command, &size, stdin);

    if (command_len == -1) {
        perror("getline error");
        exit(1);
    }
    if (command_len > MAX_INPUT_LENGTH + 1) {//+1 for \n that getLine adds
        printf("input is too long\n");
        free(command);
        return NULL;
    }
    if (command_len > 1 && command[command_len - 1] == '\n') {
        command[command_len - 1] = '\0';  // remove newline character if there are any other chars in the input
    }
    command[command_len] = 0;
    return command;
}

//get a single command, initialise argv[] with its arguments & return it to execute_multiple_commands.
//It deals with regular commands as well as with setting environment variables
//It returns 'SUCCESS' if the input is a legal command, and there were no memory allocation errors
int split_single_command(char **args, char *command, int *run_in_background) {
    if (command == NULL) { //it isn't a command
        return INVALID_INPUT;
    }
    int args_index = 0, is_echo = 0;
    char *token;
    char *var_name, *var_value;
    char command_copy[strlen(command) + 1];
    strcpy(command_copy, command);

    if (strchr(command, '=') != NULL &&
        (strstr(command, "echo\0") !=
         &command[0])) { //contains assignment & it isn't an echo command - probably a shell variable

        var_name = strtok(command_copy, "=");
        var_value = strtok(NULL, "=");

        if (var_name != NULL && var_value != NULL) {
            if (!is_assignment(var_name, var_value))//check if there isn't any space between name to value
                return INVALID_INPUT;
            if (strchr(var_value, '>') != NULL)
                return INVALID_INPUT;
            remove_spaces_and_quotes(var_name);
            remove_spaces_and_quotes(var_value);

            if (my_setenv(var_name, var_value) == SYSTEM_FAILURES)
                return SYSTEM_FAILURES;

        } else printf("assign in this pattern: <variable name>=<value>\n");
        return INVALID_INPUT;//the input is actually valid, but not a command, so INVALID_INPUT is returned
    }
    token = strtok(command_copy, SPACE);
    if (token == NULL) {//there are spaces only
        return INVALID_INPUT;
    }
    if (strcmp(token, "exit\0") == 0) { //the command contains exit
        return EXIT;
    }
    if (strcmp(token, "echo") == 0) { //echo
        is_echo = 1;
        int ret = deal_with_echo(command, args, &args_index);
        if (ret != SUCCESS)
            return ret;
    }
    if (strcmp(token, "bg") == 0) { //maybe need to put in front the last process was stopped
        if (strtok(NULL, SPACE) == NULL && (waitpid(stopped_process, NULL, WNOHANG)) == 0) {
            kill(stopped_process, SIGCONT);
            cmd_count++;
            arg_count++;
        }
        return INVALID_INPUT;//it isn't a command
    }

    while (token != NULL && !is_echo) { //there are more arguments
        if (args_index == MAX_ARGS) { //too many arguments
            fprintf(stderr, "too many arguments\n");
            args[args_index] = NULL;
            return INVALID_INPUT;
        }
        if (token[0] == '$') {//a shell variable
            var_name = token + 1;
            var_value = my_getenv(var_name);

            if (var_value == NULL) {//this variable isn't exist is my data structure
                printf("%s isn't assigned\n", var_name);
                args[args_index] = NULL; //close argv[]
                return INVALID_INPUT;
            }
            token = var_value; //very important it will be here, before the other conditions
        }
        if (strcmp(token, "cd") == 0) { //cd not supported
            fprintf(stderr, "cd not supported\n");
            return INVALID_INPUT;
        }
        if (strcmp(token, SPACE) != 0) {
            //space isn't an argument
            divide_string(token, &args[args_index], &args[args_index + 1], &args[args_index + 2], &args_index);
        }
        token = strtok(NULL, SPACE);
    }

    args[args_index] = NULL;//close args[]
    for (int i = 0; i < args_index; i++) {
        if (args[i] == NULL) {
            printf("malloc failed\n");
            return SYSTEM_FAILURES;//one of the strdups failed
        }
    }
    if (args_index >= 1) {
        if (strcmp("&", args[args_index - 1]) == 0) {//the process needs to run in background
            arg_count++;
            free(args[args_index - 1]);
            args[args_index - 1] = NULL;
            args_index--;
            (*run_in_background) = 1;
        } else if (args[args_index - 1][strlen(args[args_index - 1]) - 1] == '&') {
            arg_count++;
            args[args_index - 1][strlen(args[args_index - 1]) - 1] = 0;
            (*run_in_background) = 1;
        } else
            (*run_in_background) = 0;

        if ((args[0] != NULL && strcmp(args[0], ">") == 0) ||
            (args[args_index - 1] != NULL && strcmp(args[args_index - 1], ">") == 0)) {
            printf("enter source & dest\n");
            return INVALID_INPUT;
        }
    }
    cmd_count++;
    arg_count += (args_index);
    return SUCCESS;
}

/*handle with commands seperated by ;
gets the full message the user entered. It separates the different commands (if there are some).
sends each command to split_single_command(), which initialises argv[] or set an environment variable
if initialisation succeeded, the function sends argv[] to execute_single_command() that executes it.
 argv[] is freed here, no matter if the input is valid or not.
 It gets a pointer to command in order to free it if needed.

 CRUCIAL - uses strsep() instead of strtok() because split_single_command() uses strtok() simultaneously.
 The saved state between calls that allows strtok() to continue where it left off ruined the program.
 https://stackoverflow.com/questions/7218625/what-are-the-differences-between-strtok-and-strsep-in-c
 */
void
split_multiple_commands(char **command) {
    if (command == NULL)
        return;

    char *sub_command;
    //it isn't really a copy, but it's needed because otherwise command itself will be changed by strsep() to NULL, and we won't be able to free it
    char *copy_command = (*command);
    char *args[MAX_ARGS + 3];//for null and for check if there are too many arguments
    int is_command = 0, index = 0, is_executed = 1, is_new_command = 0;
    int run_in_background = 0, to_file = 0;

    int len = strlen(copy_command);
    int last_pos = 0;
    int inside_quotes = 0;

    for (int i = 0; i < len; i++) {
        if (copy_command[i] == '"') {
            inside_quotes = !inside_quotes;
        }
        if (copy_command[i] == ';' && !inside_quotes) {
            copy_command[i] = '\0';
            sub_command = copy_command + last_pos;
            is_new_command = 1;
            last_pos = i + 1;
        }
        if (last_pos < len && i == len - 1) {
            sub_command = copy_command + last_pos;
            is_new_command = 1;
        }
        if (sub_command[0] == 0)
            is_new_command = 0;

        if (is_new_command == 1) {
            is_new_command = 0;
            args[0] = NULL;
            if (strchr(sub_command, '|') != NULL)//pipe command
                is_executed = execute_pipe_commands(sub_command);
            else {//not pipe command
                is_command = split_single_command(args, sub_command, &run_in_background);

                if (is_command == SUCCESS) { //the command is readable (not space or null)
                    is_executed = execute_single_command(args, run_in_background,
                                                         command);//here everything happens:)
                }
            }
            index = 0;
            while (args[index] != NULL) {
                free(args[index]);
                index++;
            }

            if (is_command == SYSTEM_FAILURES || is_executed == SYSTEM_FAILURES || is_command == EXIT) {//exit
                free(*command);
                free_env_vars();
                if (is_command == EXIT)
                    exit(0);
                exit(1);
            }
        }
    }
}

void make_fork(pid_t *p) {
    (*p) = fork();
}

int make_exec(char **args) {
    int to_file = 0, fd;
    for (int i = 0; args[i + 1] != NULL; ++i) { //check if need to write to file
        if (strcmp(args[i], ">") == 0)
            to_file = i;
    }

    if (to_file > 0 && args[to_file + 1] != NULL) {//need write to file
        if(args[to_file+1][0]=='$'){
            char* t= my_getenv(args[to_file+1]+1);
            if(t==NULL) {
                printf("don't use '$' in the name of the file\n");
                return INVALID_INPUT;
            }
            else
               args[to_file+1]=t;
        }
        fd = open(args[to_file + 1], O_WRONLY | O_CREAT|O_TRUNC, 0666);//open file to read & write
        if (fd == -1) {
            printf("cannot open file\n");
            return SYSTEM_FAILURES;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        free(args[to_file]); //free indexes of args
        free(args[to_file + 1]);
        args[to_file] = NULL;
        args[to_file + 1] = NULL;
        arg_count = arg_count - 2;
    }
    execvp(args[0], args);
    perror("ececvp error"); //illegal command - execvp returned
    return INVALID_INPUT;//normally shouldn't come here
}

//executes the command in a new process by ececvp(). the father waits until his child is completed.
//if the command was illegal, it updates the counters, and free the child's memory
int execute_single_command(char **args, int run_in_background, char **command_to_free) {
    if (args == NULL || args[0] == NULL) {
        cmd_count--;
        fprintf(stderr, "no arguments\n");
        return INVALID_INPUT;
    }
    pid_t p;
    int status, exit_value;

    make_fork(&p);
    run_now = p;
    if (p < 0) {//forking failed
        perror("forking failed");
        return SYSTEM_FAILURES;
    }
    if (p == 0) {//child's process
        signal(SIGCHLD, SIG_DFL);//return deal with signals to default
        signal(SIGTSTP, SIG_DFL);

        int ret = make_exec(args);
        //illegal command - execvp returned

        //free the memory allocated to argv[] & command
        int i = 0;
        for (; args[i] != NULL; ++i)
            free(args[i]);
        free(*command_to_free);
        exit(i); //the father knows whether the command was legal by the exit value.
        // The exit value also mentions how many arguments were in the illegal command - decrease counters
    }

    // Wait for child process to complete
    if (!run_in_background) {
        if (waitpid(p, &status, WUNTRACED) == -1) {
            perror("waitpid() failed");
            //return SYSTEM_FAILURES;
        }
//        exit_value = WEXITSTATUS(status);
//        if (!WIFSTOPPED(status) && exit_value > 0) { //the command was illegal - decrease counters
//            cmd_count--;
//            arg_count -= exit_value;
//        }
    }
    return SUCCESS;
}

int execute_pipe_commands(char *command) {
    int num_pipes = 1;
    for (int i = 0; command[i] != 0; i++) {
        if (command[i] == '|')
            num_pipes++;
    }
    char *commands[num_pipes];
    int num_commands = 0; //for running on the loop
    int run_in_background = 0;

    // Split command pipeline into individual commands based on the pipe character
    char *token;
    char *saveptr;
    for (token = strtok_r(command, "|", &saveptr);
         token != NULL; token = strtok_r(NULL, "|", &saveptr)) {
        // Trim leading and trailing whitespace from token
        while (*token != '\0' && (*token == ' ' || *token == '\t')) {
            token++;
        }
        if (*token == '\0') {
            // Ignore empty tokens
            continue;
        }
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
        }

        if (token != NULL) {
            commands[num_commands] = strdup(token);  // allocate and copy command
            if (token == NULL) {
                perror("malloc failed\n");
                return SYSTEM_FAILURES;
            }
            num_commands++;
        }
    }
    if (num_commands > 0 && commands[num_commands - 1][strlen(commands[num_commands - 1]) - 1] == '&') {
        run_in_background = 1;
        arg_count++;
    }

    int prev_pipefd[2] = {-1, -1};
    int pipefd[2];
    pid_t pid;

    for (int i = 0; i < num_commands; i++) {
        if (pipe(pipefd) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        make_fork(&pid);
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {  // Child process
            if (i != 0) {
                // If not the first command, redirect input to read end of previous pipe - the output of the previous command is there!
                if (prev_pipefd[0] != -1) {
                    // If there is a previous pipe, close your path to its write end
                    close(prev_pipefd[1]);
                }

                dup2(prev_pipefd[0], STDIN_FILENO);  // Redirect input to read end of previous pipe
            }
            if (i != num_commands - 1) {
                // If not the last command, redirect output to write end of pipe
                close(pipefd[0]);  // Close unused read end
                dup2(pipefd[1], STDOUT_FILENO);  // Redirect output to write end of pipe
            }
            close(pipefd[1]);  // Close unused write end

            // Split command into individual tokens based on whitespace
            char *args[MAX_ARGS + 3];
            split_single_command(args, commands[i], &run_in_background);

            // Execute command
            make_exec(args);
            int decrease = 0;// This code shouldn't normally be reached - exec failed <=> command was invalid
            for (; args[decrease] !=
                   NULL; decrease++); //the exit value will tell the father how many arguments to decrease
            exit(decrease);

        } else {  // Parent process
            if (prev_pipefd[0] != -1) {
                close(prev_pipefd[0]);
                close(prev_pipefd[1]);
            }
            prev_pipefd[0] = pipefd[0]; // Save the read end of the current pipe for the next command
            prev_pipefd[1] = pipefd[1]; // Save the write end of the current pipe for the next command
        }
    }

    cmd_count += num_commands;
    arg_count += count_arguments(commands, num_commands);

    for (int i = 0; i < num_commands && !run_in_background; i++) {
        free(commands[i]);
    }

    int status, exit_value = 0;

    //for (int i = 0; i < num_commands && !run_in_background; i++) {
    if (!run_in_background) {
        if (waitpid(pid, &status, WUNTRACED) == -1) {
            perror("waitpid() failed here\n");
            //return SYSTEM_FAILURES;
        }
    }
//        if (WIFEXITED(status)) {
//            if ((exit_value = WEXITSTATUS(status)) > 0) {
//                printf("invalid command\n");
//                cmd_count--;
//                arg_count -= exit_value;
//            }
//        }

    return INVALID_INPUT;
}


//remove redundant spaces and quotes between words in an input string
void remove_spaces_and_quotes(char *str) {
    if (str == NULL)
        return;
    char temp[strlen(str)];
    char before = str[0];
    int index = 0;
    for (int i = 0; str[i] != 0; i++) {
        if ((str[i] != SPACE_CHAR || before != SPACE_CHAR) && str[i] != '"')
            temp[index++] = str[i];
        before = str[i];
    }
    if (index > 0 && temp[index - 1] == SPACE_CHAR)
        index--;

    temp[index] = 0;  // Add a null terminator to the end of the new string
    strcpy(str, temp);
}

void remove_quotes(char *str) {
    if (str == NULL)
        return;
    char temp[strlen(str)];
    int index = 0;
    for (int i = 0; str[i] != 0; i++) {
        if (str[i] != '"')
            temp[index++] = str[i];
    }
    temp[index] = 0;  // Add a null terminator to the end of the new string
    strcpy(str, temp);
}

//check if there aren't any spaces between name to value (should use sscanf(), but it made too much problems)
int is_assignment(char *name, char *value) {
    if (name == NULL || value == NULL)
        return 0;

    while (*(name) == SPACE_CHAR)
        name++;
    for (char *i = name + 1; i <= value; i++) {
        if (*(i) == SPACE_CHAR) {
            printf("%c\n", *i);
            printf("there is no command %s\n", name);
            return 0;
        }
    }
    return 1;
}

//initialise argv[] for echo command. its major value is to treat "input in" as one string, and 'input in' as some. also remove quotes
int deal_with_echo(char *command, char *argv[12], int *args_index) {
    int i = 0; // Index variable for argv
    int in_quotes = 0; // Flag to indicate whether the program is currently inside quotes
    char *start_word, *end_word; // Pointers to the start & the end of the current word
    char *var_value;//if there is an environment variable,it holds its value

    // Parse the input string
    start_word = command;
    end_word = command;
    while (*end_word != '\0') {
        // Skip leading spaces
        while (*start_word == ' ') {
            start_word++;
            end_word++;
        }
        // Check for quotes
        if (*start_word == '\"') {
            in_quotes = 1;
            start_word++;
        }
        // Find the end of the current word
        end_word = start_word;
        while (*end_word != '\0' && (*end_word != ' ' || in_quotes)) {
            if (*end_word == '\"') {
                in_quotes = !in_quotes;
            }
            end_word++;
        }
        // Copy the current word to the array
        int word_length = end_word - start_word;

        if (word_length > 0) {
            argv[i] = (char *) malloc((word_length + 1) * sizeof(char));
            if (argv[i] == NULL) {
                printf("Error: malloc failed\n");
                return SYSTEM_FAILURES;
            }
            strncpy(argv[i], start_word, word_length);
            argv[i][word_length] = '\0';

            if (strlen(argv[i]) > 1 && (*start_word) == '$' && argv[i][1] != '"' &&
                argv[i][1] != ' ') {//maybe it's a shell variable
                char *q = strchr(argv[i], '"'); //need to cover: <var_name>"gibberish" without spaces
                int after_name_ind = word_length;
                if (q != NULL) {
                    after_name_ind = q - argv[i];
                    (*q) = 0;
                }

                var_value = my_getenv(argv[i] + 1);
                if (var_value != NULL) {//it's an environment variable!
                    free(argv[i]);

                    argv[i] = (char *) malloc((strlen(var_value) + (word_length - after_name_ind)) * sizeof(char));
                    if (argv[i] == NULL) {
                        printf("Error: malloc failed\n");
                        return SYSTEM_FAILURES;
                    }
                    strcpy(argv[i], var_value);
                    if (q != NULL) {
                        char p = (*end_word);
                        (*end_word = 0);//so it will copy only till the end of the word
                        strcat(argv[i], start_word + after_name_ind);
                        (*end_word) = p;
                    }
                } else
                    strcpy(argv[i], " "); //print space only
            }
            remove_quotes(argv[i]);

            if (strchr(argv[i], '>') != NULL) {
                char copy[(strlen(argv[i]))];
                strcpy(copy, argv[i]);
                free(argv[i]);
                divide_string(copy, &argv[i], &argv[i + 1], &argv[i + 2], &i);
                //divide-string increases the counter
            } else
                i++;

            // Handle too many words in the input string: echo + strings
            if (i > MAX_ARGS) {
                printf("Error: Too many words in input string\n");
                argv[i] = NULL;
                return INVALID_INPUT;
            }
        }
        start_word = end_word;  // Move to the next word
    }

    // Handle unbalanced quotes at end of string
    if (in_quotes != 0) {
        printf("Error: Unbalanced quotes in input string\n");
        argv[i] = NULL;
        return INVALID_INPUT;
    }

    *(args_index) = i;  // Update number of arguments
    return SUCCESS;
}

//divide string according to '>'(write to file) character
void divide_string(char *str, char **first, char **second, char **third, int *arg_index) {
    char copy_str[strlen(str)];
    strcpy(copy_str, str);
    char *ptr = strchr(copy_str, '>');
    if (ptr == NULL) {
        // ">" not found in string
        *first = strdup(copy_str);
        (*arg_index)++;
    } else {
        *ptr = '\0'; // replace ">" with null character
        if (ptr == copy_str) {
            // ">" is first character in string
            *first = strdup(">");
            if (strcmp(str, ">") == 0) { //use str because copy_str was changed
                (*arg_index)++;
                return;
            }
            *second = strdup(ptr + 1);
            (*arg_index) += 2;
        } else if (ptr == copy_str + strlen(str) - 1) { //use str because copy_str was changed
            // ">" is last character in string
            *first = strdup(copy_str);
            *second = strdup(">");
            (*arg_index) += 2;
            // *ptr = '\0'; // replace ">" with null character
        } else {
            // ">" is in the middle of the string
            *first = strdup(copy_str);
            *second = strdup(">");
            *third = strdup(ptr + 1);
            (*arg_index) += 3;
        }
    }
}

int count_arguments(char **arr, int n) {
    int count = 0;
    int in_quote = 0;
    for (int i = 0; i < n; i++) {
        char *str = arr[i];
        int len = strlen(str);
        for (int j = 0; j < len; j++) {
            if (str[j] == '"') {
                in_quote = !in_quote;
            } else if (!in_quote && str[j] == ' ') {
                count++;
            }
            if (str[j] == '>') {//count > argument
                if (str[j - 1] != ' ' && str[j + 1] != ' ')
                    count += 2;
                else if (str[j - 1] != ' ' || str[j + 1] != ' ')
                    count += 1;
            }
        }
        if (!in_quote && len > 0 && str[len - 1] != ' ') {
            count++; // count last word in string
        }
    }
    return count;
}

void catch_child(int sig) {
    signal(SIGCHLD, catch_child);
    waitpid(-1, NULL, WNOHANG);
}


void catch_stop(int sig) {
    signal(SIGTSTP, catch_stop);
    if ((waitpid(run_now, NULL, WNOHANG)) == 0) //there is a killed child
        stopped_process = run_now;
    //kill(run_now, SIGTSTP);  // don't need to send signal - they are all from the same group
}

/********************************************* ENVIRONMENT VARIABLES MANAGEMENT ****************************************************************/
//The setenv() function takes a name and a value as arguments, and searches through the existing environment variables to see if the name already exists.
// If it does, the value is updated; if not, a new variable is added to the end of the list
int my_setenv(char *name, char *value) {
    int i;
    for (i = 0; i < env_var_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            free(env_vars[i].value);  // free existing value
            env_vars[i].value = strdup(value);  // allocate new value
            return SUCCESS;
        }
    }
    if (env_var_count >= MAX_ENV_VARS) {
        fprintf(stderr, "Error: maximum number of environment variables exceeded\n");
        return SYSTEM_FAILURES;
    }
    env_vars[env_var_count].name = strdup(name);  // allocate name
    if (env_vars[env_var_count].name == NULL) {
        fprintf(stderr, "Error: failed to allocate memory for environment variable name\n");
        return SYSTEM_FAILURES;
    }
    env_vars[env_var_count].value = strdup(value);  // allocate value
    if (env_vars[env_var_count].value == NULL) {
        fprintf(stderr, "Error: failed to allocate memory for environment variable value\n");
        free(env_vars[env_var_count].name);  // free name
        return SYSTEM_FAILURES;
    }
    env_var_count++;
    return SUCCESS;
}

//takes a name as an argument, and searches through the environment variables to find the matching name, and returns the corresponding value.
char *my_getenv(char *name) {
    int i;
    for (i = 0; i < env_var_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].value;
        }
    }
    return NULL;
}

void free_env_vars() { //frees the data structure
    int i;
    for (i = 0; i < env_var_count; i++) {
        free(env_vars[i].name);
        free(env_vars[i].value);
    }
    env_var_count = 0;
}

