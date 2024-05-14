#include "kernel/types.h"
#include "user/user.h"

#define MIN_NUM 2
#define MAX_NUM 35

#define PIPELINE_SIZE 20

/*
Your goal is to use pipe and fork to set up the pipeline. The first process feeds the
numbers 2 through 35 into the pipeline. For each prime number, you will arrange
to create one process that reads from its left neighbor over a pipe and writes to its
right neighbor over another pipe. Since xv6 has limited number of file descriptors
and processes, the first process can stop at 35.
*/

struct pipelinecache {
  int this_pid;
  int pipe_fd[2];
  int index;
  int eliminate_val;
};

struct pipelinecache pipeline_array[PIPELINE_SIZE];

int feed_list[MAX_NUM + 1];

void readforkpipe(struct pipelinecache* ptr) {
  int temp_data;
  close(ptr->pipe_fd[1]);  // 关闭写，重要

  // printf("readforkpipe, index: %d, eliminate: %d\n", ptr->index, ptr->eliminate_val);
  int n = 1;
  while(n) {
    n = read(ptr->pipe_fd[0], &temp_data, 4);
    if (!n) {
      close(ptr->pipe_fd[0]);
      break;
    }

    if (ptr->eliminate_val == temp_data) {
      printf("prime %d\n", ptr->eliminate_val);
      feed_list[ptr->eliminate_val] = -1;
    }

    if (temp_data % ptr->eliminate_val == 0) {
      feed_list[temp_data] = -1;
    }
  }

}

void writeforkpipe(struct pipelinecache* ptr) {
  close(ptr->pipe_fd[0]);    // 关闭读

  for (int i = MIN_NUM; i <= MAX_NUM; i++) {
    if (feed_list[i] != -1) {
      write(ptr->pipe_fd[1], &i, 4);
    }
  }
}

int get_eliminate() {
  for (int i = MIN_NUM; i <= MAX_NUM; i++) {
    if (feed_list[i] != -1)  return feed_list[i];
  }
  return -1;
}

void closewritefd(struct pipelinecache* ptr) {
  close(ptr->pipe_fd[1]);
}


// 每一层级的流水线做的事情：
// 0. 创建流水线结构体，初始化
// 1. 读入数据
// 2. 处理数据
// 3. 保存临时结果，准备进入第二级流水线
// 4. 释放上一层流水线的资源
void createpipeline(int index, int eliminate_val) {
  if (index > PIPELINE_SIZE) {
    fprintf(2, "pipeline out of array...\n");
    exit(1);
  }

  struct pipelinecache* ptr = &pipeline_array[index];
  ptr->index = index;
  ptr->eliminate_val = eliminate_val;
  if (-1 == eliminate_val) {
    // 释放最后一个pipeline
    struct pipelinecache* prev_ptr = &pipeline_array[index - 1];
    closewritefd(prev_ptr);
    return;
  }

  if (pipe(ptr->pipe_fd) < 0) {
    fprintf(2, "pipe failed...\n");
    exit(1);
  }

  ptr->this_pid = fork();
  if (ptr->this_pid < 0) {
    fprintf(2, "fork failed...\n");
    exit(1);
  }

  if (ptr->this_pid == 0) {
    readforkpipe(ptr);
  } else {
    writeforkpipe(ptr);
    closewritefd(ptr);
    // 先等待之前的进程结束，然后还需要主动exit
    wait(0);
    exit(0);
  }
}

// 设计方案：
// 将 2-35的数据，一起push同一层级的流水线，而不是一个数据一个数据的push
// 好处是，前面的流水线用完后，可以进行销毁的操作

int main(int argc,char *argv[]) {

  for (int i = MIN_NUM; i <= MAX_NUM; i++) {
    feed_list[i] = i;
  }

  int pipeline_index, eliminate_val;
  for (pipeline_index = 0; ; pipeline_index ++) {
    eliminate_val = get_eliminate();

    createpipeline(pipeline_index, eliminate_val);

    if (-1 == eliminate_val) {
      break;
    }
  }

  exit(0);
}
