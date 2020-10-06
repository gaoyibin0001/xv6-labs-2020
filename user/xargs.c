#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

// pipe2.c: communication between two processes

int
main(int argc, char *argv[])
{
//   int i, fd;
//   for(i = 2; i < argc; i++){
//     if(strlen(argv[i]) == 1 && ){
//       printf("grep: cannot open %s\n", argv[i]);
//       exit(1);
//     }
//     grep(pattern, fd);
//     close(fd);
//   printf("the argc %d\n", argc);
//     printf("the argv %s\n", argv[0]);
//     printf("the argv %s\n", argv[1]);
//     printf("the argv %s\n", argv[2]);

  char c;
//   int fds[2];
//   pipe(fds);
//   close(fds[1]);
  int pid;

  char *args[MAXARG];
  int i;
  for(i = 1; i < argc; i++)
        args[i-1] = argv[i]; 

  int k=0;
  int m = argc - 1;
  int n=0;
  char buf[MAXARG][55];
  while(1){
    int cc = read(0, &c, 1);
    if(cc < 0){
      printf("read() failed in countfree()\n");
      exit(1);
    }
    if(cc == 0)
      break;
    // printf("the cc %d, %s\n", cc, c);

    // if ((c == '\n') | (cc == 0)) {
    if (c == '\n') {

        if (k != 0) {
            buf[n][k] = '\0';
            k = 0;
            // char buf_tmp[512];
            // strcpy(buf_tmp, buf);
            args[m] = buf[n];
            args[m+1] = 0;
            m = argc - 1;
            n = 0;
       }

    //    printf("the buff character %s\n", buf[n]);

        pid = fork();
        if (pid == 0) {
            exec(argv[1], args);
        } else {
            wait(0);
        }

        // if(cc == 0)
        //   break;
    } else if (c == ' ') {
        // printf("the buff character %s\n", buf);
       if (k != 0) {
        buf[n][k] = '\0';
        k = 0;

        // char buf_tmp[512];
        // strcpy(buf_tmp, buf);
        args[m] = buf[n];

        // args[m] = buf;
        m += 1;
        n += 1;

        // printf("the buff character %s\n", buf);
       }

    } else {
        // printf("the k character %d\n", k);
        buf[n][k] = c;
        k++;
    }

  }

//   close(fds[0]);
  exit(0);
}