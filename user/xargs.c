#include "kernel/types.h"
#include "user/user.h"

#include "kernel/param.h"

/**
$ echo hello too | xargs echo bye
 bye hello too
*/

#if 0
  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      // p[1] <- 输出 写入
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      // p[0] -> 读取 输入
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]); // p[0] 可读取
    close(p[1]); // p[1] 可写
    // finish
    wait(0);
    wait(0);
    break;

#endif

#define MAX_BUF_LEN 256

// 返回读取的argv list的数组长度
int parse_args(char* buf, int* index, char* argv_list[], int argc, char* argv[], int unix_flag) {
  int arg_len;
  int argv_list_size = 0;
  int list_index = 0;
  int i = 1;
  if (unix_flag)  { i = 3; }
  // printf("parse_args argc: %d\n", argc);
  for (; i < argc; i++) {
    arg_len = strlen(argv[i]);
    memcpy(buf + *index, argv[i], arg_len);
    argv_list[list_index] = buf + *index;
    *index += arg_len + 1;
    // printf("parse_args %s\n", argv_list[list_index]);
    argv_list_size ++;
    list_index ++;
  }

  // printf("parse_args: argv_list_size: %d\n", argv_list_size);
  return argv_list_size;
}


int get_stdin_str(char* buf, char* argv_list[], int* argv_list_size/*, int unix_flag*/) {
  int i;
  char c;
  int ret;
  int last_len = MAX_BUF_LEN - 1;
  char* old_buf = buf;
  for (i=0; i < last_len; i++) {
    ret = read(0, &c, 1);
    if (ret < 1) { // 包含 0 -1 等情况，0 表示连接断开
      // printf("get_stdin_str ret: %d\n", ret);
      break;
    }
    // printf("%d\n", c);
    // printf("get_stdin_str %d\n", c);
    if (c == '\n') {
      argv_list[(*argv_list_size)++] = old_buf;
      buf[i] = '\0';
      old_buf = buf + i + 1;   // yes, old_buf 指向buf + i的下一个位置
    } else {
      buf[i] = c;
    }
  }

  return i;
}

// xargs 可以读取标准输入，并通过标准输入是否关闭来

// 先处理标准情况，再处理unix_flag的情况
int main(int argc, char *argv[]) {

  if (argc <= 1 ) {
    fprintf(2, "usage: xargs  cmd\n");
    exit(1);
  }

  int unix_flag = 0;
  if (strcmp(argv[1], "-n") == 0 && strcmp(argv[2], "1") == 0) {
    unix_flag = 1;
  }

  char buf[MAX_BUF_LEN];
  int index = 0;
  char *argv_list[MAXARG];
  int fd;

  // clean
  memset(buf, 0, sizeof(buf));

  int argv_list_size = parse_args(buf, &index, argv_list, argc, argv, unix_flag);
  if (argv_list_size < 1) {
    fprintf(2, "parse_args  failed\n");
    exit(1);
  }

  char buf_stdin[MAX_BUF_LEN];
  memset(buf_stdin, 0, sizeof(buf_stdin));

  char *stdin_list[MAXARG];
  int stdin_list_size = 0;
  if (unix_flag) {
    if (!get_stdin_str(buf_stdin, stdin_list, &stdin_list_size/*, unix_flag*/)) {
      fprintf(2, "get_stdio_str  failed\n");
      exit(1);
    }
  } else {
    // unix_flag 处理策略：启动多个进程来处理每一个参数
    if (!get_stdin_str(buf_stdin, argv_list, &argv_list_size/*, unix_flag*/)) {
      fprintf(2, "get_stdio_str  failed\n");
      exit(1);
    }
  }

  // for (int i = 0; i <= argv_list_size; i++) {
  //   printf("%s\n", argv_list[i]);
  // }
  // printf("#######\n");

  // 每次只对一行的数据执行 exec(argv_list)的操作
  if (unix_flag) {
    for (int i = 0; i < stdin_list_size; i++) {
      fd = fork();
      if (fd == 0) {
        argv_list[argv_list_size] = stdin_list[i];
        exec(argv_list[0], argv_list);
      } else {
        wait(0);
      }
    }
  } else {
    fd = fork();
    if (fd == 0) {
      exec(argv_list[0], argv_list);
    } else {
      wait(0);
    }
  }

  exit(0);
}
