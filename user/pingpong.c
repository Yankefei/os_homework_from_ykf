#include "kernel/types.h"
#include "user/user.h"

const char data = 'a';

void readforkpipe(int fds[]) {
  char temp_data;
  close(fds[1]);  // 关闭写，重要
  
  int n = read(fds[0], &temp_data, 1);
  if (n != 1 || temp_data != data) {
    fprintf(2, "child read...\n");
    exit(1);
  }
}

void writeforkpipe(int fds[]) {
  close(fds[0]);    // 关闭读
  int n = write(fds[1], &data, 1);
  if (n != 1) {
      fprintf(2, "child write...\n");
      exit(1);
  }
}

int main(int argc,char *argv[]) {
  int pid, fds_ping[2], fds_pong[2];


  if (pipe(fds_ping) < 0 || pipe(fds_pong) < 0) {
    fprintf(2, "pipe failed...\n");
    exit(1);
  }

  pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed...\n");
    exit(1);
  }

  if (pid == 0) {
    // child
    readforkpipe(fds_ping);

    int this_pid = getpid();
    printf("%d: received ping\n", this_pid);

    writeforkpipe(fds_pong);
  } else {
    writeforkpipe(fds_ping);
    readforkpipe(fds_pong);

    int this_pid = getpid();
    printf("%d: received pong\n", this_pid);

    wait(&pid);
  }

  exit(0);
}
