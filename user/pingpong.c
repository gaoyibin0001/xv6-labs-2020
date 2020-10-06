#include "kernel/types.h"
#include "user/user.h"

// pipe2.c: communication between two processes

int
main()
{
  int n, pid;
  int fds[2];
  char buf[1];
  
  // create a pipe, with two FDs in fds[0], fds[1].
  pipe(fds);
  
  pid = fork();
  if (pid == 0) {
    n = read(fds[0], buf, sizeof(buf));
    fprintf(2, "%d: received ping\n", getpid());
    write(fds[1], buf, n);
  } else {
    write(fds[1], " ", 1);
    wait(0);
    n = read(fds[0], buf, sizeof(buf));
    fprintf(2, "%d: received pong\n", getpid());
  }

  exit(0);
}