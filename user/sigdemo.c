// 综合演示：
// 1) 开一个子进程，注册（几乎）所有信号的处理器并依次测试；
//    对不可捕获/不可忽略的 9(SIGKILL)/19(SIGSTOP) 单独开子进程测试，避免中断主子进程。
// 2) 再针对 32 个“未注册 handler”的子进程分别发送信号，观察默认反应。

// 采用相对路径以兼容编辑器的 includePath 设置
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "signal.h"

static void generic_handler(int signum) {
  printf("sigdemo(child %d): 捕获信号 %d\n", getpid(), signum);
  sigreturn();
}

enum act { ACT_IGNORE, ACT_TERMINATE, ACT_TERMINATE_CORE, ACT_STOP, ACT_CONTINUE };

static enum act default_action(int s) {
  switch(s) {
  case 3: case 4: case 5: case 6: case 7: case 8: case 11: case 24: case 25: case 31:
    return ACT_TERMINATE_CORE;
  case 1: case 2: case 9: case 13: case 15: case 16: case 26: case 27: case 29: case 30:
    return ACT_TERMINATE;
  case 19: case 20: case 21: case 22:
    return ACT_STOP;
  case 18:
    return ACT_CONTINUE;
  case 10: case 12: case 14: case 17: case 23: case 28:
    return ACT_IGNORE;
  default:
    return ACT_IGNORE; // 包含 0 号信号
  }
}

static void register_handlers_all(void) {
  for(int s = 0; s < 32; s++) {
    // 9(SIGKILL) 与 19(SIGSTOP) 不可注册
    if(s == 9 || s == 19) {
      int r = signal(s, generic_handler);
      if(r < 0) {
        printf("sigdemo(child %d): 信号 %d 不可注册处理器\n", getpid(), s);
      }
      continue;
    }
    int r = signal(s, generic_handler);
    if(r < 0) {
      printf("sigdemo(child %d): 注册信号 %d 处理器失败\n", getpid(), s);
    }
  }
}

static void child_loop(void) {
  // 用较低占用的方式等待信号
  while(1) {
    pause(20);
  }
}

static void test_signal_on_temp_child(int sig) {
  int t = fork();
  if(t < 0) {
    printf("sigdemo(parent %d): fork 失败，无法为信号 %d 创建临时子进程\n", getpid(), sig);
    return;
  }
  if(t == 0) {
    printf("sigdemo(temp child %d): 准备接收信号 %d\n", getpid(), sig);
    child_loop();
    exit(0);
  }
  pause(10);
  printf("sigdemo(parent %d): -> 发送信号 %d 给 temp 子进程 %d\n", getpid(), sig, t);
  sigsend(t, sig);
  enum act a = default_action(sig);
  if(a == ACT_STOP) {
    pause(20);
    printf("sigdemo(parent %d): -> SIGCONT 唤醒 temp 子进程 %d\n", getpid(), t);
    sigsend(t, SIGCONT);
    pause(20);
    printf("sigdemo(parent %d): -> SIGKILL 结束 temp 子进程 %d\n", getpid(), t);
    sigsend(t, SIGKILL);
    wait(0);
  } else if(a == ACT_IGNORE) {
    pause(20);
    printf("sigdemo(parent %d): -> SIGKILL 结束 temp 子进程 %d (默认忽略)\n", getpid(), t);
    sigsend(t, SIGKILL);
    wait(0);
  } else {
    // 终止或终止+core
    wait(0);
  }
}

int main(int argc, char *argv[]) {
  printf("sigdemo: 第一阶段（注册处理器并测试 0..31）\n");

  // 第一阶段：主子进程注册处理器并测试所有信号；9/19 单独用临时子进程测试
  int c = fork();
  if(c == 0) {
    register_handlers_all();
    printf("sigdemo(child %d): 处理器已注册，等待信号...\n", getpid());
    child_loop();
    exit(0);
  }

  pause(20);
  for(int s = 0; s < 32; s++) {
    if(s == 9 || s == 19) {
      printf("sigdemo(parent %d): (特殊) 测试不可捕获/不可忽略信号 %d 于临时子进程\n", getpid(), s);
      test_signal_on_temp_child(s);
      continue;
    }
    printf("sigdemo(parent %d): -> 发送信号 %d 给主子进程 %d\n", getpid(), s, c);
    sigsend(c, s);
    pause(15);
  }
  printf("sigdemo(parent %d): 第一阶段结束，SIGKILL 结束主子进程 %d\n", getpid(), c);
  sigsend(c, SIGKILL);
  wait(0);

  // 第二阶段：针对每个信号创建一个未注册 handler 的子进程并测试默认反应
  printf("sigdemo: 第二阶段（未注册 handler 的默认行为测试 0..31）\n");
  for(int s = 0; s < 32; s++) {
    int d = fork();
    if(d == 0) {
      printf("sigdemo(default child %d): 等待默认反应的信号 %d\n", getpid(), s);
      child_loop();
      exit(0);
    }
    pause(10);
    printf("sigdemo(parent %d): -> 发送信号 %d 给 default 子进程 %d\n", getpid(), s, d);
    sigsend(d, s);
    enum act a = default_action(s);
    switch(a) {
      case ACT_IGNORE:
        pause(20);
        printf("sigdemo(parent %d): 默认忽略，SIGKILL 清理子进程 %d\n", getpid(), d);
        sigsend(d, SIGKILL);
        wait(0);
        break;
      case ACT_STOP:
        pause(20);
        printf("sigdemo(parent %d): 默认停止，发送 SIGCONT 唤醒 %d，然后 SIGKILL 结束\n", getpid(), d);
        sigsend(d, SIGCONT);
        pause(20);
        sigsend(d, SIGKILL);
        wait(0);
        break;
      case ACT_CONTINUE:
        pause(20);
        printf("sigdemo(parent %d): 默认继续，SIGKILL 清理子进程 %d\n", getpid(), d);
        sigsend(d, SIGKILL);
        wait(0);
        break;
      case ACT_TERMINATE:
      case ACT_TERMINATE_CORE:
        // 进程会自行终止，等待即可
        wait(0);
        break;
    }
  }

  printf("sigdemo: 所有测试完成\n");
  exit(0);
}