# Lab 2 信号机制

## 运行方法

`make clean && make qemu`，在 xv6 中运行 `sigdemo`。

## POSIX标准下的32个信号

本实验实现并测试了 POSIX 标准下常见的 32 个信号（编号范围 0..31，其中 0 号在本实现中用于占位/保留，默认忽略）。下表列出 1..31 的信号名称、默认动作和简要说明：

| 信号编号 | 信号名         | 默认动作              | 说明                           |
|----------|----------------|-----------------------|--------------------------------|
| 1        | SIGHUP         | 终止                  | 挂起（终端断开连接）           |
| 2        | SIGINT         | 终止                  | 中断（通常是 Ctrl+C）          |
| 3        | SIGQUIT        | 终止 + core dump      | 退出（通常是 Ctrl+\）         |
| 4        | SIGILL         | 终止 + core dump      | 非法指令                       |
| 5        | SIGTRAP        | 终止 + core dump      | 跟踪/断点陷阱（调试器使用）    |
| 6        | SIGABRT        | 终止 + core dump      | 异常终止（abort() 调用）       |
| 7        | SIGBUS         | 终止 + core dump      | 总线错误（内存对齐或访问错误） |
| 8        | SIGFPE         | 终止 + core dump      | 浮点异常（如除零）             |
| 9        | SIGKILL        | 终止                  | 强制终止（不可捕获、不可忽略） |
| 10       | SIGUSR1        | 终止                  | 用户自定义信号 1               |
| 11       | SIGSEGV        | 终止 + core dump      | 无效内存引用（段错误）         |
| 12       | SIGUSR2        | 终止                  | 用户自定义信号 2               |
| 13       | SIGPIPE        | 终止                  | 向无读端的管道写数据           |
| 14       | SIGALRM        | 终止                  | 闹钟定时器超时（alarm()）      |
| 15       | SIGTERM        | 终止                  | 终止请求（可被捕获，优雅退出） |
| 16       | SIGSTKFLT      | 终止                  | 协处理器栈故障（Linux 特有）   |
| 17       | SIGCHLD        | 忽略                  | 子进程状态改变                 |
| 18       | SIGCONT        | 继续                  | 继续执行（恢复被暂停的进程）   |
| 19       | SIGSTOP        | 停止                  | 暂停进程（不可捕获、不可忽略） |
| 20       | SIGTSTP        | 停止                  | 终端暂停（通常是 Ctrl+Z）      |
| 21       | SIGTTIN        | 停止                  | 后台进程尝试从终端读取         |
| 22       | SIGTTOU        | 停止                  | 后台进程尝试向终端写入         |
| 23       | SIGURG         | 忽略                  | 套接字紧急数据到达             |
| 24       | SIGXCPU        | 终止 + core dump      | CPU 时间超限                   |
| 25       | SIGXFSZ        | 终止 + core dump      | 文件大小超限                   |
| 26       | SIGVTALRM      | 终止                  | 虚拟定时器超时                 |
| 27       | SIGPROF        | 终止                  | Profiling 定时器超时           |
| 28       | SIGWINCH       | 忽略                  | 终端窗口大小改变               |
| 29       | SIGIO / SIGPOLL| 终止                  | I/O 可用（异步 I/O 通知）      |
| 30       | SIGPWR         | 终止                  | 电源故障（System V）           |
| 31       | SIGSYS         | 终止 + core dump      | 无效系统调用                   |

注：信号编号在不同架构上可能略有不同，但名称和默认行为通常一致。本实现设置 `NSIG=32`，覆盖 0..31；其中 0 号信号作为占位，默认忽略。

## 实现：trap.c 和 sysproc.c 介绍

- 信号范围与常量
  - `NSIG=32`，覆盖编号 0..31；用户态头文件 `user/signal.h` 定义了全部信号常量与名称。
  - 进程结构增加信号相关字段：`pending_signals`（待递送位图）、`handlers[]` 与 `handlers_registered`（用户态处理器及注册位图）、`tf_backup`/`tf_backup_valid`（处理器上下文备份）、`stopped`（STOP/CONT 状态）。

- trap.c（默认动作与递送）
  - 在 `usertrap()` 返回用户态前扫描 `pending_signals`，若该信号已注册处理器，则备份当前 `trapframe` 并跳转到处理器；否则执行默认动作。
  - 默认动作覆盖：
    - 终止类：`SIGTERM`、`SIGPIPE`、`SIGSTKFLT`、以及扩展的 `SIGIO`、`SIGPWR` 等，调用 `setkilled()`。
    - 终止+core：`SIGQUIT`、`SIGILL`、`SIGTRAP`、`SIGABRT`、`SIGBUS`、`SIGFPE`、`SIGSEGV`、`SIGXCPU`、`SIGXFSZ`、`SIGSYS`，调用 `dump_core(p, signum)` 并标记终止。
    - 忽略类：`SIGCHLD`、`SIGURG`、`SIGWINCH`、以及 `SIGUSR1/2`、`SIGALRM` 等，直接清除待递送位。
    - STOP 类：`SIGSTOP`、`SIGTSTP`、`SIGTTIN`、`SIGTTOU` 默认进入休眠。为避免锁重入，使用 `sleep(&p->stopped, &wait_lock)`，并在进入前设置 `p->stopped = 1`。
    - CONTINUE：`SIGCONT` 默认清除待递送位并继续，不在此处主动唤醒；实际唤醒由 `sys_sigsend(SIGCONT)` 完成。
  - 头文件包含次序按需调整，确保 `file.h`/`stat.h` 等依赖解析正确。

- sysproc.c（系统调用接口）
  - `signame(int)` 和 `sigdefault(int)` 返回信号名称与默认行为字符串，用于调试打印与用户态演示。
  - `sys_signal(signum, handler)`：禁止为 `SIGSTOP` 注册处理器（符合不可捕获属性）。其余信号允许注册，记录到 `handlers[]` 与 `handlers_registered`。
  - `sys_sigsend(pid, signum)`：将目标进程的 `pending_signals` 置位，并处理特殊信号：
    - `SIGKILL`：在持有 `tp->lock` 下直接标记 `tp->killed = 1`，无需再次获取锁。
    - `SIGCONT`：为避免 `wakeup()` 与 `tp->lock` 的锁重入，流程为：释放 `tp->lock` → 获取 `wait_lock` → 重新获取 `tp->lock` → 若 `tp->stopped` 则先置 `stopped=0`，释放 `tp->lock` 后调用 `wakeup(&tp->stopped)`，最后释放 `wait_lock`。该顺序消除了 `panic: acquire`。
  - `sys_sigreturn()`：从用户态处理器返回，恢复备份的 `trapframe` 并清除有效位。

- core dump
  - `dump_core(p, signum)` 在 `trap.c` 中实现，生成 `core.<pid>.<signum>` 文件，便于定位触发致命信号的上下文。

## 测试：sigdemo 的测试流程

- 第一阶段：注册并测试 0..31
  - 父进程 `fork` 一个“主子进程”，为 0..31 注册通用处理器（9/19 不可注册则提示）。
  - 父进程按序发送信号到主子进程；遇到 9(SIGKILL)/19(SIGSTOP) 时，改用“临时子进程”分别测试，避免终止/暂停主子进程。
  - STOP 测试路径：对临时子先 `SIGSTOP`，再 `SIGCONT` 唤醒，最后 `SIGKILL` 清理。
  - 其余信号由处理器打印“捕获信号 N”并调用 `sigreturn()` 返回。

- 第二阶段：未注册处理器的默认行为验证（0..31）
  - 为每个信号创建一个不注册 handler 的子进程，分别发送该信号以观察默认反应。
  - 忽略类：稍作等待后用 `SIGKILL` 清理；停止类：`SIGCONT` 唤醒后再 `SIGKILL` 清理；继续类：稍作等待后 `SIGKILL` 清理；终止/终止+core 类：直接 `wait` 等待退出。

- 运行与观察
  - 在 shell 运行 `sigdemo`；输出会包含父/子进程侧日志和 `trap`/`sigsend` 的调试信息，便于核对每类信号的默认动作与处理器路径。
  - 也可使用 `sigsend <pid> <signum>` 手工向目标进程发送信号补充测试。

## 分阶段实验过程

首先，实现 `SIGINT` 信号的注册与处理。发现信号需要默认行为于是在 `trap.c` 中添加。为了测试注册后修改的信号行为，在 `sigdemo.c` 中添加了 `SIGINT` 处理器，打印“捕获信号 2”并调用 `sigreturn()` 返回。

然后实现 `SIGKILL` 和几个常见非 core dump 信号的处理。实现方法类似，不过 `SIGKILL` 不允许用户注册处理器，采取了措施防止。

发现如果先执行 `SIGINT` 并且进程的相关处理器未注册成功，后续几个信号将发送至死进程，这样会导致 `SIGKILL` 的默认终止进程行为无法正确执行，产生 `panic: acquire` 错误。于是排查并修复了处理器注册，并尝试对 `SIGKILL` 采用更安全的做法：标记进程的 `killed` 标志位，而不是直接终止进程。

然后实现简单的 core dump 功能并实现了几个 core dump 信号。

接下来实现了几个常规信号，和 `SIGSTOP`、`SIGCONT` 两个特殊信号。`SIGSTOP` 信号也需要与 `SIGKILL` 相似的安全处理，而 `SIGCONT` 信号则需要在 `sys_sigsend` 中特殊处理，避免锁重入。

通过修改测试手段为先单进程测试注册再开多个进程测试默认效果，可以按部就班实现每个信号的处理和测试。
