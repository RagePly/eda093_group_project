/*
 * Main source file for the lsh shell program.
 *
 * You are free to add functions to this file.
 * If you want to add functions in separate files,
 * you will need to modify CMakeLists.txt to compile
 * your additional files.
 *
 * Add appropriate comments to make your code
 * easier for us to grade.
 *
 * Using assert statements is a good way to catch errors early and make debugging easier.
 * Think of them as mini self-checks that ensure your program behaves as expected.
 * By setting up these guardrails, you're creating a more robust and maintainable solution.
 * So go ahead, sprinkle some asserts in your code; they're your friends in disguise!
 *
 * All the best!
 */
#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

// The <unistd.h> header is your gateway to the OS's process management facilities.
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "parse.h"

/* Forward Declarations */
static void print_cmd(Command *cmd);
static void print_pgm(Pgm *p);
void stripwhite(char *);
void exit_cleanup(int retcode);
void handle_cmd(Command *p);
int handle_builtin(char **args);
void builtin_cd(char **args);


struct RunInfo {
  int stdin_fd, stdout_fd;
  int is_interactive;
  char *program;
  char **args;
};

pid_t run_program(struct RunInfo *run_info);

struct JobHandle {
  pid_t pid;
  struct JobHandle *next;
};

struct JobHandle* job_push(struct JobHandle *top);
struct JobHandle* job_pop(struct JobHandle *top);
void job_await(struct JobHandle *handle, int terminate);
/* End declarations */

/* Linked list of process handles
 */
struct JobHandle *g_jobs = NULL;

int main(void)
{
  /* Ignore interactive interrupts. This setting is inherited by child-processes
   */
  signal(SIGINT, SIG_IGN);

  /* Ignore child terminatation/stop signals, meaning the child-process is
   * reaped by the OS upon exit/termination. (man-page _Exit(3p))
   */
  signal(SIGCHLD, SIG_IGN);

  for (;;)
  {
    char *line;
    line = readline("> ");

    /* CTRL-D encountered
     * MAN: readline returns NULL if EOF is encountered on a blank line.
     *      If line is not blank, EOF treated as a newline and the subsequent
     *      readline returns the NULL string.
     */
    if (line == NULL)
    {
      exit_cleanup(0);
    }

    // Remove leading and trailing whitespace from the line
    stripwhite(line);

    // If the stripped line is not blank
    if (*line)
    {
      add_history(line);

      Command cmd;
      if (parse(line, &cmd) == 1)
      {
        // Print the parsed command
        print_cmd(&cmd);

        handle_cmd(&cmd);
      }
      else
      {
        printf("Parse ERROR\n");
      }
    }

    // Free the input buffer
    free(line);
  }

  return 0;
}

/* Exit the shell, cleaning up any child processes
 */
void exit_cleanup(int retcode)
{
  while (g_jobs)
  {
    /* Send a SIGTERM signal to the child
     */
    job_await(g_jobs, 1);
    g_jobs = job_pop(g_jobs);
  }

  exit(retcode);
}

/*
 * Print a Command structure as returned by parse on stdout.
 *
 * Helper function, no need to change. Might be useful to study as inspiration.
 */
static void print_cmd(Command *cmd_list)
{
  printf("------------------------------\n");
  printf("Parse OK\n");
  printf("stdin:      %s\n", cmd_list->rstdin ? cmd_list->rstdin : "<none>");
  printf("stdout:     %s\n", cmd_list->rstdout ? cmd_list->rstdout : "<none>");
  printf("background: %s\n", cmd_list->background ? "true" : "false");
  printf("Pgms:\n");
  print_pgm(cmd_list->pgm);
  printf("------------------------------\n");
}

/* Print a linked list of Pgm structures.
 *
 * Helper function, no need to change. It may be useful to study for inspiration.
 */
static void print_pgm(Pgm *p)
{
  if (p == NULL)
  {
    return;
  }
  else
  {
    char **pl = p->pgmlist;

    /* The list is stored in reverse order, so print
     * it in reverse to restore the original order.
     */
    print_pgm(p->next);
    printf("            * [ ");
    while (*pl)
    {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}


/* Strip whitespace from the start and end of a string.
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string)
{
  size_t i = 0;

  while (isspace(string[i]))
  {
    i++;
  }

  if (i)
  {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i]))
  {
    i--;
  }

  string[++i] = '\0';
}

int handle_builtin(char **args) 
{
 if (args == NULL || args[0] == NULL) 
 {
  return 0;
 }
 if (strcmp(args[0], "cd") == 0)
 {
  builtin_cd(args);
  return 1;
 }
 if (strcmp(args[0], "exit") == 0)
 {
  exit_cleanup(0);
 }
 return 0;
}

void builtin_cd(char **args)
{
  char *dir = args[1];

  if (dir == NULL)
  {
    dir = getenv("HOME");
  }

  if (chdir(dir) != 0)
  {
    perror("cd failed");
  }
}

pid_t run_program(struct RunInfo *run_info)
{
  pid_t pid = fork();

  if (pid) return pid;

  /* This process and it's children should use the default SIGCHLD handler
   */
  signal(SIGCHLD, SIG_DFL);

  /* The process should be terminated when an interactive interrupt
   * is issued (CTRL-C)
   */
  if (run_info->is_interactive) signal(SIGINT, SIG_DFL);

  if (run_info->stdin_fd != STDIN_FILENO)
  {
    dup2(run_info->stdin_fd, STDIN_FILENO);
  }

  if (run_info->stdout_fd != STDOUT_FILENO)
  {
    dup2(run_info->stdout_fd, STDOUT_FILENO);
  }

  int error_id = execvp(run_info->program, run_info->args);

  perror("failed to launch program");
  exit(error_id);
}


void handle_cmd(Command *p)
{
  if (!p->pgm) return;

  if (handle_builtin(p->pgm->pgmlist)) return; 

  if (p->pgm->next)
  {
    fprintf(stderr, "TODO: handle pipe:ing\n");
    exit_cleanup(1);
  }
  
  int stdin_fd = STDIN_FILENO;
  int stdout_fd = STDOUT_FILENO;

  if (p->rstdin)
  {
    int fd = open(p->rstdin, O_RDONLY);
    if (fd < 0) 
    {
      perror("could not open file for reading");
      return;
    }
    stdin_fd = fd;
  }

  if (p->rstdout)
  {
    int fd = open(p->rstdout, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) 
    {
      perror("could not open file for writing");

      if (stdin_fd != STDIN_FILENO) close(stdin_fd);
      return;
    }
    stdout_fd = fd;
  }

  struct RunInfo ri = {
    .stdin_fd = stdin_fd,
    .stdout_fd = stdout_fd,
    .is_interactive = !p->background,
    .program = p->pgm->pgmlist[0],
    .args = p->pgm->pgmlist,
  };

  pid_t child = run_program(&ri);

  /* Close files */
  if (stdin_fd != STDIN_FILENO) close(stdin_fd);
  if (stdout_fd != STDOUT_FILENO) close(stdout_fd);

  /* if the job is to be run in the background, push it to the 
   * job list
   */
  if (p->background)
  {
    g_jobs = job_push(g_jobs);
    g_jobs->pid = child;
  }
  /* The job is run in the foreground, await termination
   */
  else
  {
    (void)waitpid(child, NULL, 0);
  }
}

/* Allocate an uninitialized handle and push it to the linked-list
 */
struct JobHandle* job_push(struct JobHandle *top)
{
  struct JobHandle *handle = malloc(sizeof(struct JobHandle));

  if (!handle)
  {
    perror("failed to allocate job handle");
    exit_cleanup(1);
  }

  handle->pid = -1;
  handle->next = top;

  return handle;
}

/* Remove the handle from the linked-list, freeing allocated memory
 * (the process is not awaited/terminated!)
 */
struct JobHandle* job_pop(struct JobHandle *top)
{
  if (top == NULL)
  {
    fprintf(stderr, "cannot pop last element");
    exit_cleanup(1);
  }

  struct JobHandle *next = top->next;
  free(top);
  return next;
}

/* Await the process, optionally sending a termination signal 
 */
void job_await(struct JobHandle *handle, int terminate)
{
  if (handle->pid <= 0) return;

  if (terminate) kill(handle->pid, SIGTERM);

  (void)waitpid(handle->pid, NULL, 0);
}
