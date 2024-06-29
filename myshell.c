#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include "LineParser.h"
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0

typedef struct process {
    cmdLine* cmd;             /* the parsed command line*/
    pid_t pid;                /* the process id that is running the command*/
    int status;               /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next;     /* next process in chain */
} process;

process* process_list = NULL;

int debugMode = 0;

pid_t currentPID = 0;

#define HISTLEN 20
#define MAX_BUF 200

char history[HISTLEN][MAX_BUF];
int newest = 0;
int oldest = 0;
int histCount = 0;

cmdLine ** parsedCmdAddresses;
int cmdCount = 0;
int cmdSize = HISTLEN;

void addProcess(process** process_list, cmdLine* cmd, pid_t pid) {
    process* new_process = malloc(sizeof(process));
    new_process->cmd = cmd;
    new_process->pid = pid;
    new_process->status = RUNNING;
    new_process->next = NULL;

    if (*process_list == NULL) {
        *process_list = new_process;
    } else {
        process* current = *process_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_process;
    }
}

void freeProcessList(process* process_list) {
    process* tmp;

    while (process_list != NULL) {
        tmp = process_list;
        process_list = process_list->next;
        // if(tmp->cmd != NULL)
        //     freeCmdLines(tmp->cmd);
        free(tmp);
    }

    process_list = NULL;

}

int addToCmdArray(cmdLine* cmd){
    if (cmdCount >= cmdSize) {
        
        parsedCmdAddresses = (cmdLine**)realloc(parsedCmdAddresses, 2 * cmdSize * sizeof(cmdLine*));
        if (parsedCmdAddresses == NULL){
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < cmdSize; i++){
            parsedCmdAddresses[cmdSize + i] = NULL;
        }
        cmdSize *= 2;
    }
    parsedCmdAddresses[cmdCount++] = cmd;
    return 1;
}


void freeCmdAddresses(cmdLine** addresses, int len){
    for (int i = 0; i < cmdSize; i++){
        if(parsedCmdAddresses[i]) 
            freeCmdLines((cmdLine*)parsedCmdAddresses[i]);
        
    }
    free(addresses);
}

void updateProcessList(process **process_list) {
    int status;
    process* tmp = *process_list;

    while (tmp != NULL) {
        if (waitpid(tmp->pid, &status, WNOHANG) == tmp->pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                tmp->status = TERMINATED;
            } else if (WIFSTOPPED(status)) {
                tmp->status = SUSPENDED;
            } else if (WIFCONTINUED(status)) {
                tmp->status = RUNNING;
            }
        }
        tmp = tmp->next;
    }
}

void updateProcessStatus(process* process_list, int pid, int status) {
    while (process_list != NULL) {
        if (process_list->pid == pid) {
            process_list->status = status;
            return;
        }
        process_list = process_list->next;
    }
    fprintf(stderr, "Error: process with PID %d not found\n", pid);
}

void printProcessList(process** process_list) {
    updateProcessList(process_list);

    process* curr = *process_list;
    process* prev = NULL;
    int index = 1;

    printf("Index\tPID\tStatus\tCommand\n");
    while (curr != NULL) {
        printf("%d\t%d\t%s\t%s\n", index, curr->pid,
                curr->status == RUNNING ? "Running" :
                curr->status == SUSPENDED ? "Suspended" : "Terminated",
                curr->cmd->arguments[0]);

        if (curr->status == TERMINATED) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                *process_list = curr->next;
            }

            process* tmp = curr;
            curr = curr->next;
            freeCmdLines(tmp->cmd);
            free(tmp);
        } else {
            prev = curr;
            curr = curr->next;
        }
        index++;
    }
}

void execute(cmdLine *pCmdLine) {
    int pipefd[2];
    if (pCmdLine->next != NULL) {
        if (pipe(pipefd) == -1) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }
    }

    int pid = fork();
    if (pid == -1) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    //child
    if (pid == 0) {

        FILE *input = stdin;

        //redirect input
        if (pCmdLine->inputRedirect != NULL) {
            close(STDIN_FILENO);
            if (open(pCmdLine->inputRedirect, O_RDONLY) == -1) {
                perror("Error: opening input file");
                _exit(EXIT_FAILURE);
            }
            dup2(fileno(input), STDIN_FILENO);
        }
        //redirect output
        if (pCmdLine->outputRedirect != NULL) {
            if (pCmdLine->next != NULL) {
                fprintf(stderr, "Error: output redirection in piped command\n");
                _exit(EXIT_FAILURE);
            }
            close(STDOUT_FILENO);
        }
        //redirect to pipe
        if (pCmdLine->next != NULL) {
            close(STDOUT_FILENO);
            dup(pipefd[1]);
            close(pipefd[1]);
        }
        //execute
        if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            perror("Error: executing command");
            _exit(EXIT_FAILURE);
        }
    }
    //parent
    else {
        currentPID = pid;
        if (debugMode){
            fprintf(stderr, "PID: %d\n", pid);
            fprintf(stderr, "Executing command: %s\n", pCmdLine->arguments[0]);
        }
        if (pCmdLine->next != NULL) {
            close(pipefd[1]);
            int pid2 = fork();
            if (pid2 == -1) {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }

            if (pid2 == 0) {
                //redirect input
                if (pCmdLine->next->inputRedirect != NULL) {
                    fprintf(stderr, "Error: input redirection in piped command\n");
                    _exit(EXIT_FAILURE);
                }
                
                //redirect output
                if (pCmdLine->next->outputRedirect != NULL) {
                    close(STDOUT_FILENO);
                    if (open(pCmdLine->next->outputRedirect, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR) == -1) {
                        perror("Error: opening output file");
                        _exit(EXIT_FAILURE);
                    }
                    printf("outputRedirect: %s\n", pCmdLine->next->outputRedirect);
                    dup2(fileno(stdout), STDOUT_FILENO);
                }

                close(STDIN_FILENO);
                dup(pipefd[0]);
                close(pipefd[0]);
                //execute
                if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
                    perror("Error: executing command");
                    _exit(EXIT_FAILURE);
                }
            }
            else {
                close(pipefd[0]);
                if (pCmdLine->blocking) {
                    int status;
                    waitpid(pid, &status, 0);
                    waitpid(pid2, &status, 0);
                }
            }
        }
        addProcess(&process_list, pCmdLine , currentPID);
        if (pCmdLine->blocking) {
            int status;
            waitpid(pid, &status, 0);
            updateProcessStatus(process_list, currentPID , TERMINATED);
        }
    }
}

void printHistory() {
    for (int i = 0; i < histCount; i++) {
        printf("%d %s", i + 1, history[(oldest + i) % HISTLEN]);
    }
    printf(" ");
}

void executeHistory(int index) {
    if (index < 0 || index >= histCount) {
        printf("No such command in history.\n");
        return;
    }
    char* cmd = history[(oldest + index) % HISTLEN];
    cmdLine* parsedCmd = parseCmdLines(cmd);
    if (parsedCmd == NULL) 
        fprintf(stderr, "Error: parsing command failed\n");
    else 
        addToCmdArray(parsedCmd);
    printf("%s\n", cmd);
    if ( strcmp(parsedCmd->arguments[0],"history") == 0 ) {
        printHistory();
    }
    // else if (strcmp(parsedCmd->arguments[0], "sleep") == 0){
    //     if (kill(atoi(parsedCmd->arguments[1]), SIGTSTP) == -1){
    //         perror("Error: sleep operation failed");
    //     } else {
    //         updateProcessStatus(process_list, atoi(parsedCmd->arguments[1]), SUSPENDED);
    //     }
    // }
    else if (strcmp(parsedCmd->arguments[0], "procs") == 0){
        printProcessList(&process_list);
    }
    else if (strcmp(parsedCmd->arguments[0], "cd") == 0){
        if (chdir(parsedCmd->arguments[1]) == -1){
            perror("Error: cd operation failed");
        }
    }
    else if (strcmp(parsedCmd->arguments[0], "alarm") == 0){
        if (kill(atoi(parsedCmd->arguments[1]), SIGCONT) == -1){
            perror("Error: alarm operation failed");
        } else {
            updateProcessStatus(process_list, atoi(parsedCmd->arguments[1]), RUNNING);
        }
    }
    else if (strcmp(parsedCmd->arguments[0], "blast") == 0){
        if (kill(atoi(parsedCmd->arguments[1]), SIGINT) == -1){
            perror("Error: blast operation failed");
        } else {
            updateProcessStatus(process_list, atoi(parsedCmd->arguments[1]), TERMINATED);
        }
    }
    else{
        execute(parsedCmd);
    }

    // freeCmdLines(parsedCmd);
}

int main(int argc, char **argv){

    parsedCmdAddresses = (cmdLine**)malloc(HISTLEN * sizeof(cmdLine*));
    if (parsedCmdAddresses == NULL){
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-d") == 0){
            debugMode = 1;
            break;
        }
    }

    char cwd[PATH_MAX];
    char line[2048];
    while (1){
        getcwd(cwd, sizeof(cwd));
        printf("%s$ ", cwd);
        fgets(line, sizeof(line), stdin);
        // int freeCmdLine = 0;
        int dontIncludeInHistory = 0;
        cmdLine *pCmdLine = parseCmdLines(line);
        if (pCmdLine == NULL) {
            fprintf(stderr, "Error: parsing command failed\n");
            continue;
        }
        else 
            addToCmdArray(pCmdLine);
        
        if (strcmp(pCmdLine->arguments[0], "quit") == 0){
            // freeCmdLines(pCmdLine);
            
            break;
        }
        else if ( strcmp(pCmdLine->arguments[0],"history") == 0 ){
            strncpy(history[newest], line , MAX_BUF);
            newest = (newest + 1) % HISTLEN;
            if (histCount < HISTLEN) {
                histCount++;
            } else {
                oldest = (oldest + 1) % HISTLEN;
            }
            printHistory();
            dontIncludeInHistory = 1;
        }
        else if ( strcmp(pCmdLine->arguments[0],"!!") == 0 ){
            int index = (newest-1) % HISTLEN;
            executeHistory(index);
            dontIncludeInHistory = 1;
        }
        else if ( pCmdLine->arguments[0][0] == '!' ){
            int index = atoi(pCmdLine->arguments[0] + 1);
            executeHistory(index - 1);
            dontIncludeInHistory = 1;
        }
        // else if (strcmp(pCmdLine->arguments[0], "sleep") == 0){
        //     if (kill(atoi(pCmdLine->arguments[1]), SIGTSTP) == -1){
        //         perror("Error: sleep operation failed");
        //     } else {
        //         updateProcessStatus(process_list, atoi(pCmdLine->arguments[1]), SUSPENDED);
        //     }
        // }
        else if (strcmp(pCmdLine->arguments[0], "procs") == 0){
            printProcessList(&process_list);
        }
        else if (strcmp(pCmdLine->arguments[0], "cd") == 0){
            if (chdir(pCmdLine->arguments[1]) == -1){
                perror("Error: cd operation failed");
            }
        }
        else if (strcmp(pCmdLine->arguments[0], "alarm") == 0){
            if (kill(atoi(pCmdLine->arguments[1]), SIGCONT) == -1){
                perror("Error: alarm operation failed");
            } else {
                updateProcessStatus(process_list, atoi(pCmdLine->arguments[1]), RUNNING);
            }
        }
        else if (strcmp(pCmdLine->arguments[0], "blast") == 0){
            if (kill(atoi(pCmdLine->arguments[1]), SIGINT) == -1){
                perror("Error: blast operation failed");
            } else {
                updateProcessStatus(process_list, atoi(pCmdLine->arguments[1]), TERMINATED);
            }
        }
        else{
            execute(pCmdLine);
            // freeCmdLine = 0;
        }
        // if(freeCmdLine){
        //     freeCmdLines(pCmdLine);
        // }
        if(!dontIncludeInHistory){
            strncpy(history[newest], line , MAX_BUF);
            newest = (newest + 1) % HISTLEN;
            if (histCount < HISTLEN) {
                histCount++;
            } else {
                oldest = (oldest + 1) % HISTLEN;
            }
        }
    }
    freeProcessList(process_list);
    freeCmdAddresses(parsedCmdAddresses, HISTLEN);
    
    return 0;
}
