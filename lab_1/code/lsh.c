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
void handle_cmd(Command *cmd);
int handle_builtin(char **args);
void builtin_cd(char **args);
static int count_pgms(Pgm *p);

struct JobHandle {
  pid_t pid;
  struct JobHandle *next;
};

struct JobHandle* job_push(struct JobHandle *top);
struct JobHandle* job_pop(struct JobHandle *top);
struct JobHandle* job_join(struct JobHandle *top, struct JobHandle *bottom);
void job_await(struct JobHandle *handle, int terminate);
void job_await_all(struct JobHandle** handle, int terminate);
/* End declarations */

/* Linked list of process handles
 */
struct JobHandle *g_jobs = NULL;
struct JobHandle *g_foreground = NULL;

int main(void)
{
  /*
   * The shell ignores ctrl-c
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
        // running the command pipeline
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

  job_await_all(&g_jobs, 1);

  /* FIXME: this might not be necessary since we're calling exit, but it's 
   * probably good housekeeping. What's the approach?
   */
  job_await_all(&g_foreground, 1);

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


void handle_cmd(Command *cmd)
{
  if (!cmd->pgm) return;

  /* Only handle builtins if there is no pipeing (same as in sh) */
  if (!cmd->pgm->next && handle_builtin(cmd->pgm->pgmlist)) return;

  int n = count_pgms(cmd->pgm);
  /* Allocate a VLA on the stack, this is automatically freed on scope exit
   */
  int pipefds[2 * (n > 1 ? n - 1 : 0)];

  /* Create n-1 pipes up front */
  for (int i = 0; i < n - 1; i++)
  {
    if (pipe(&pipefds[i * 2]) < 0)
    {
      perror("pipe");
      exit_cleanup(1);
    }
  }

  /*
   * Walk the (reverse-order) list and assign each Pgm an index
   * from 0 (first command executed) to n-1 (last command executed),
   * so pipefds[i] connects command i's stdout to command i+1's stdin.
   */
  Pgm *p = cmd->pgm;
  int index = n - 1; // p starts at the LAST command

  if (g_foreground)
  {
    fprintf(stderr, "can't spawn new processes if foreground processes are running\n");
    exit_cleanup(1);
  }

  while (p != NULL)
  {
    pid_t pid = fork();

    if (pid == 0)
    {
      /* This child process should be terminated on SIGINT, it's just the shell
       * that handles this differently...
       */
      signal(SIGINT, SIG_DFL);

      /* ...however, if this is a background process, it should be in it's own process
       * group so that the CTRL-C initiated SIGINT does not reach it
       */
      if (cmd->background)
      {
        if (setsid() < 0)
        {
          perror("failed to change process group for background process");
          exit(1);
        }
      }

      /* This process and it's children should use the default SIGCHLD handler
       */
      signal(SIGCHLD, SIG_DFL);

      /* stdin: from previous pipe, unless this is the first command
       */
      if (index == 0)
      {
        if (cmd->rstdin != NULL)
        {
          int fd = open(cmd->rstdin, O_RDONLY);
          if (fd < 0)
          {
            perror("open rstdin");
            exit(1);
          }
          dup2(fd, STDIN_FILENO);
          close(fd);
        }
      }
      else
      {
        dup2(pipefds[(index - 1) * 2], STDIN_FILENO);
      }

      /* stdout: to next pipe, unless this is the last command
       */
      if (index == n - 1)
      {
        if (cmd->rstdout != NULL)
        {
          int fd = open(cmd->rstdout, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd < 0)
          {
            perror("open rstdout");
            exit(1);
          }
          dup2(fd, STDOUT_FILENO);
          close(fd);
        }
      }
      else
      {
        dup2(pipefds[index * 2 + 1], STDOUT_FILENO);
      }


      /* Close all pipe fds in the child, since the ones that are
       * still in use are duplicated to stdin/stdout
       */
      for (int i = 0; i < 2 * (n - 1); i++)
      {
        close(pipefds[i]);
      }

      execvp(p->pgmlist[0], p->pgmlist);
      perror("execvp");
      exit(1);
    }
    else if (pid > 0)
    {
      g_foreground = job_push(g_foreground);
      g_foreground->pid = pid;
    }
    else
    {
      perror("fork");
      exit_cleanup(1);
    }

    p = p->next;
    index--;
  }

  /* Parent: close all children */
  for (int i = 0; i < 2 * (n - 1); i++)
  {
    close(pipefds[i]);
  }

  /* if the jobs are to be run in the background, push it to the 
   * job list
   */
  if (cmd->background)
  {
    g_jobs = job_join(g_foreground, g_jobs);
    g_foreground = NULL;
  }
  /* The jobs are run in the foreground, await termination
   */
  else
  {
    job_await_all(&g_foreground, 0);
  }
}

/* Count how many programs are in the pipeline */
static int count_pgms(Pgm *p)
{
  int n = 0;
  while (p != NULL)
  {
    n++;
    p = p->next;
  }

  return n;
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

/* Join two linked lists
 */
struct JobHandle* job_join(struct JobHandle *top, struct JobHandle *bottom)
{
  if (!top) return bottom;

  struct JobHandle *top_last = top;
  while (top_last->next)
  {
    top_last = top_last->next;
  }

  top_last->next = bottom;
  return top;
}

void job_await_all(struct JobHandle** handle, int terminate)
{
  while (*handle)
  {
    job_await(*handle, terminate);
    *handle = job_pop(*handle);
  }
}
