//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

// 对于循环引用的软连接，是可以被创建成功，但是，打开的时候
// 会判断并报错
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if ((omode & O_NOFOLLOW) == 0) {
      // 多次针对 ip 加锁和解锁
      if ((ip = namei(path)) == 0) {
        end_op();
        return -1;
      }

      while (1) {
        ilock(ip);
        if (ip->type != T_SYMLINK) {
          iunlock(ip);
          break;
        }
        char n_path_name[MAXPATH];
        int len = readi(ip, 0, (uint64)n_path_name, 0, MAXPATH);
        if (len < 0) {
          panic("open symlink reai failed");
        }
        int path_len = strlen(path);

        // 校验 是否存在循环引用
        if (len == path_len && (memcmp(n_path_name, path, len) == 0)) {
          iunlock(ip);
          end_op();
          return -1;
        }
        iunlock(ip);

        if ((ip = namei(n_path_name)) == 0) {
          end_op();
          return -1;
        }
      }
    } else {
      if((ip = namei(path)) == 0){
        end_op();
        return -1;
      }
    }

    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

// 确认
struct inode*
getTargetFileInode(char *src_path, char* src_name) {

  // target 的父目录inode 和 对应name
  struct inode* t_dp, *t_ip;
  if((t_dp = nameiparent(src_path, src_name)) == 0)
    return 0;

  // 寻找 src_name 是否在 t_dp的目录inode中存在
  ilock(t_dp);
  if((t_ip = dirlookup(t_dp, src_name, 0)) == 0) {
    // 存在，则返回
    iunlockput(t_dp);
    return 0;
  }

  iunlockput(t_dp);
  return t_ip;
}

/**
 * 目前 fs 里面的接口，可以直接通过path来查找 inode, 但暂时不能通过inode的inum信息来反查 path
 * 所以软链接的策略，只能是保存path，而不是inode的 inum.
 * 
 * You will need to choose somewhere to store the target path of a symbolic link, for example, in the inode's data blocks.
 *
 * symlink(target, path)   path(symlink) ->  target
 * 
 *  1. 软链接的名称不能和对应目录或文件的名称一样
    2. 对同一个文件，不能连续创建2个名称一样的软链接
    3. 如果target文件不存在，那么将创建一个空的path inode节点
    4. 如果 path的路径异常，则直接报错
*/

// 可以被 sys_unlink 清理
uint64
sys_symlink(void) {
  char target[MAXPATH];
  char path[MAXPATH];
  char t_name[DIRSIZ];
  char p_name[DIRSIZ];
  // 确认target的文件是否存在
  int target_exit = 1;
  int target_dev;

  if (argstr(0, target, MAXPATH) < 0) {
    return -1;
  }
  if (argstr(1, path, MAXPATH) < 0) {
    return -1;
  }

  begin_op();
  struct inode* p_dp, *p_ip;

  // 先读取 path 的信息，如果没找到对应的目录，则直接报错
  if ((p_dp = nameiparent(path, p_name)) == 0) {
    end_op();
    return -1;
  }

  // 获取target的目录信息, 如果不存在，则后面会创建一个空的 symlink
  struct inode* t_ip = getTargetFileInode(target, t_name);
  if (!t_ip) {
    target_exit = 0;
  }

  // 寻找 p_name 是否在 p_dp的目录inode中已经存在了
  ilock(p_dp);
  if((p_ip = dirlookup(p_dp, p_name, 0)) != 0) {
    // 如果已经存在，则直接报错，不能创建重复 symlink
    iunlockput(p_dp);
    end_op();
    return -1;
  }
  iunlock(p_dp);

  // 校验 是否存在循环引用
  // 暂时不用，放在open中校验
#if 0
  if (0 && t_ip) {
    struct inode* next_ip = t_ip;
    while(1) {
      if (!next_ip) {
        break;
      }

      ilock(next_ip);
      if (next_ip->type == T_SYMLINK) {
        char n_path_name[MAXPATH];
        int len = readi(next_ip, 0, (uint64)n_path_name, 0, MAXPATH);
        int path_len = strlen(path);
        // cycle
        if (len == path_len && (memcmp(n_path_name, path, len) == 0)) {
          iunlock(next_ip);
          end_op();
          return -1;
        } else {
          char n_name[DIRSIZ];
          iunlock(next_ip);
          //new  inode
          next_ip = getTargetFileInode(n_path_name, n_name);
        }
      } else {
        iunlock(next_ip);
        break;
      }
    }
  }
#endif

  // 还需要维护的只有  t_ip  和  p_ip 了

  // todo
  // 单独创建一个空的inode ? yes
  // ialloc里面，会预先设置dinode的type字段
  // 下面ilock里面会加载t_ip->type

  if (t_ip)
    ilock(t_ip);
  target_dev = (target_exit == 0 ? ROOTDEV : t_ip->dev);
  if((p_ip = ialloc(target_dev, T_SYMLINK)) == 0) {
    if (t_ip)
      iunlockput(t_ip);
    end_op();
    return -1;
  }

  ilock(p_ip);

  if (t_ip) {
    p_ip->major = t_ip->major;
    p_ip->minor = t_ip->minor;
  }
  p_ip->nlink = 1;
  p_ip->size = 0;
  // 以下几种，都已经在 ialloc 的 iget里面设置了
  // p_ip->dev = t_ip->dev;
  // p_ip->ref = 1;
  // p_ip->valid;
  // p_ip->inum;

  // 这里商定:
  // 1. 用 p_ip->addrs[0] 来表示指向保存target path的字符串信息的 data block
  // 2. 用 p_ip->addrs[1] 来表示字符串信息的长度
  // 3. 用 p_ip->addrs[2] 来保存执行target inode的inum
  // 4. 用 p_ip->addrs[3] 来表示当前软连接的层数
  if (!target_exit) {
    p_ip->addrs[2] = 0;
    p_ip->addrs[3] = 1;
  } else {
    p_ip->addrs[2] = t_ip->inum;
    if (t_ip->type == T_SYMLINK)
      p_ip->addrs[3] = t_ip->addrs[3] + 1;
    else
      p_ip->addrs[3] = 1;
  }

  if (p_ip->addrs[3] > MAX_SYMLINK_LEVEL) {
    goto fail;
  }

  uint64 ret, len;
  len = strlen(target);

  // 申请block，并写入target字符串时，完全可以直接复用writei的函数
  // target 的长度一定小于单个block的大小, 所以基本上肯定会写入 addrs[0]的位置
  if ((ret = writei(p_ip, 0, (uint64)target, 0, len)) != len) {
    panic("sys_symlink write target failed"); 
  }

  if (p_ip->addrs[0] == 0) {
    printf("sys_symlink addrs[0] is empty...\n");
    panic("sys_symlink addrs[0] is empty"); 
  }
  p_ip->addrs[1] = len;
  // 更新到磁盘中
  iupdate(p_ip);

  // 在目录中创建对应文件
  ilock(p_dp);
  if(dirlink(p_dp, p_name, p_ip->inum) < 0) {
    // printf("dirlink failed\n");
    iunlockput(p_dp);
    goto fail;
  }
  // 有可能会更新p_dp
  iunlockput(p_dp);

  iunlockput(p_ip);
  if (t_ip)
    iunlockput(t_ip);

  end_op();

  return 0;

 fail:
  // something went wrong. de-allocate ip.
  // 清理时，只需要将 nlink 设为0即可
  p_ip->nlink = 0;
  iupdate(p_ip);
  iunlockput(p_ip);
  if (t_ip)
    iunlockput(t_ip);
  end_op();
  return -1;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
