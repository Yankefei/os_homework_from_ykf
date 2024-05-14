mkdir a
echo hello > a/b
mkdir c
echo hello > c/b
echo hello > b
# find . b | xargs -n 1 grep hello   #  pass test, 但只能提前将文件创建好，第一次执行脚本的时候，会报错, 如下
: << EOF
$ sh < xargstest.sh
$ $ $ $ $ $ grep: cannot open #
grep: cannot open #
grep: cannot open #
$ exec # failed
get_stdio_str  failed
$ $ QEMU: Terminated
EOF
find . b | xargs grep hello
