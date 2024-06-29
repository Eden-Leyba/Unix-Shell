#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    int pipefd[2];
    pipe(pipefd);
    fprintf(stderr, "(parent_process>forking…)\n");
    pid_t pid1 = fork();
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid1);

    if (pid1 == 0) {
        fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe…)\n");
        close(1);
        dup(pipefd[1]);
        close(pipefd[1]);
        char *args[] = {"ls", "-l", NULL};
        fprintf(stderr, "(child1>going to execute cmd: ls -l)\n");
        execvp(args[0], args);
        exit(1);
    }

    fprintf(stderr, "(parent_process>closing the write end of the pipe…)\n");
    close(pipefd[1]);

    fprintf(stderr, "(parent_process>forking…)\n");
    pid_t pid2 = fork();
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid2);

    if (pid2 == 0) {
        fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe…)\n");
        close(0);
        dup(pipefd[0]);
        close(pipefd[0]);
        char *args[] = {"tail", "-n", "2", NULL};
        fprintf(stderr, "(child2>going to execute cmd: tail -n 2)\n");
        execvp(args[0], args);
        exit(1);
    }

    fprintf(stderr, "(parent_process>closing the read end of the pipe…)\n");
    close(pipefd[0]);
    
    fprintf(stderr, "(parent_process>waiting for child processes to terminate…)\n");
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    fprintf(stderr, "(parent_process>exiting…)\n");

    return 0;
}
