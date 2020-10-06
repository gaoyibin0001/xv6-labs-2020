#include "kernel/types.h"
#include "user/user.h"

void
process_prims(int fds[2])
{
    int n, m, pid, k;
    close(fds[1]);
    k = read(fds[0], &n, sizeof(n));
    if (k<=0){
        // fprintf(2, "nums %d\n", k);
        exit(0);
    }
    fprintf(2, "prime %d\n", n);
    int fds2[2];
    // create a pipe, with two FDs in fds[0], fds[1].
    pipe(fds2);
    pid = fork();
    if (pid == 0) {
        process_prims(fds2);
    } else {
        close(fds2[0]);
        while(1){
            k = read(fds[0], &m, sizeof(m));
            // fprintf(2, "nums %d\n", k);
            if (k<=0){
                // fprintf(2, "nums %d\n", k);
                break;
            }
            
            if (m%n != 0){
                // fprintf(2, " %d \n", m);
                write(fds2[1], &m, sizeof(m));
            }
        }
        close(fds[0]);
        close(fds2[1]);
        wait(0);
        
    }
  exit(0);
}



int
main()
{
  int pid, i;
  int fds[2];  
  // create a pipe, with two FDs in fds[0], fds[1].
  pipe(fds);
  
  pid = fork();
  if (pid == 0) {
    process_prims(fds);
  } else {
    close(fds[0]);
    for (i=2; i<=35; i++){
        // fprintf(2, " %d \n", i);
        write(fds[1], &i, sizeof(i));
    }
    close(fds[1]);
    wait(0);
    
  }
  exit(0);
}
