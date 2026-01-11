# Lab 1-2：mmap 实验报告

## 总体目标

- 给 xv6 增加 `mmap`/`munmap`，只实现“文件映射”的必要子集，且按需（懒分配）处理缺页。
- 满足 `user/mmaptest.c` 的用例即可，同时保证 `usertests -q` 仍然通过。
- ![result](mmap_result.png)

## 功能范围

- `mmap(addr=0, len, prot, flags, fd, offset=0)`：返回由内核选择的映射起始虚拟地址；失败返回 `0xffffffffffffffff`。
- `prot` 只考虑 `PROT_READ`、`PROT_WRITE`（以及两者同时）。
- `flags` 只实现 `MAP_SHARED` 或 `MAP_PRIVATE`。
- 懒分配：`mmap` 本身不分配物理页、不读文件；发生页故障时才分配页并读入文件内容。
- `munmap(addr, len)`：解除映射；若是 `MAP_SHARED` 且页面有修改，要写回文件（本实验不强制用脏位，允许写回所有被解除的页）。

## 实现方式

- 在内核增加系统调用和用户态包装，让 `mmaptest` 能编译运行：
  - 在 `kernel/syscall.h` 增加 `SYS_mmap`、`SYS_munmap` 的编号。
  - 在 `kernel/syscall.c` 的系统调用表中注册 `sys_mmap`、`sys_munmap`。
  - 在 `kernel/sysfile.c` 或 `kernel/sysproc.c` 中实现 `sys_mmap`、`sys_munmap`，用 `argint`/`argaddr`/`argfd` 取参数；暂时先返回错误，确保编译。
  - 在 `user/usys.pl` 增加 `mmap`、`munmap` 的用户态包装；在 `user/user.h` 声明原型。
  - 在 `Makefile` 的 `UPROGS` 中加入 `_mmaptest`。
- 为每个进程维护 VMA（虚拟内存区域）表：
  - 在 `kernel/proc.h` 定义 `struct vma`，并在 `struct proc` 内加入一个固定数组（例如 16 个槽），记录：
    - `addr`（起始虚拟地址，页对齐）
    - `len`（映射字节长度，原始长度；也可保存页数）
    - `prot`（`PROT_READ`/`PROT_WRITE`）
    - `flags`（`MAP_SHARED`/`MAP_PRIVATE`）
    - `struct file *f`（被映射文件；`mmap` 时用 `filedup(f)` 增加引用）
    - `used` 标记
  - 可以额外保存 `file_offset`（本实验固定为 0），便于以后扩展。
- 选择映射地址并建立 VMA：
  - `mmap` 收到 `addr=0`，需要内核选择一个未占用的虚拟地址区间。
  - 简化做法：选在“堆之后、栈之前”的空闲区，例如定义一个固定基准（如 `MMAPBASE`）并在其上向上增长，或从 `p->sz` 的页对齐处开始向上分配不与现有页表冲突的区间。
  - `addr` 要 `PGROUNDUP`；`len` 对应的页数用 `(PGROUNDUP(addr + len) - addr) / PGSIZE` 计算。
  - 把新 VMA 放到进程的 VMA 表里，增加 `file` 的引用计数。
- 缺页处理（用户态访问触发）：
  - 在 `kernel/trap.c` 的 `usertrap()` 中，捕获 `scause` 为加载/存储页故障（RISC‑V：`load page fault`=13，`store page fault`=15；指令页故障=12）。
  - 取得故障地址 `va = r_stval()`，查找是否落在某个 VMA 范围内。
  - 若命中：
    - 计算该页相对文件的偏移：`file_offset + (PGROUNDDOWN(va) - vma.addr)`。
    - 分配物理页 `kalloc()`。
    - 给页设置权限位：`PTE_U | PTE_V | (prot & PROT_READ ? PTE_R : 0) | (prot & PROT_WRITE ? PTE_W : 0)`。若不需执行权限可不设 `PTE_X`。
    - 读文件 4096 字节到页内存：
      - 通过 `ip = vma->f->ip`，`ilock(ip)` → `readi(ip, page, file_offset, PGSIZE)` → `iunlock(ip)`。
      - 注意：如果映射长度不足一页的最后部分，多读不会有问题；也可把超出 `len` 的部分填 0。
    - `mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)page, perm)` 建立映射。
  - 若未命中 VMA，即正常的缺页；保持原有处理（通常杀死进程）。
- 实现 `munmap(addr, len)`：
  - 查找与 `(addr, len)` 重叠的 VMA，实验保证只会“从头/从尾/整个区间”解除，不会中间打洞。
  - 对涉及的页：
    - 若 `MAP_SHARED`，写回文件：
      - 可简化为“对要解除的每个页都写回”，不检查脏位。
      - 用 `begin_op()` → `ilock(ip)` → `writei(ip, page, file_offset_of_this_page, write_len)` → `iunlock(ip)` → `end_op()`。
      - `write_len` 通常是 `PGSIZE`；针对最后不足一页的情况写实际长度。
    - 用 `uvmunmap(p->pagetable, PGROUNDOWN(addr), npages, 1)` 解除映射并释放物理页。
  - 更新/缩短或删除 VMA：
    - 如果正好解除整个 VMA，清空该槽并对 `file` 做 `fileclose()`（或减少引用，取决于 xv6 的 `file` 引用策略；在 `mmap` 时用了 `filedup`，这里对应减少一次）。
    - 如果只解除前半段或后半段，调整 `vma.addr`/`vma.len`。
- 在进程退出时写回并清理：
  - 在 `kernel/proc.c` 的 `exit()` 中，遍历进程的 VMA 表：
    - 对 `MAP_SHARED` 的已映射页按上述逻辑写回。
    - 调用与 `munmap` 等价的清理逻辑（解除映射、释放页、减少文件引用）。
- 在 `fork()` 时继承映射：
  - 在 `kernel/proc.c` 的 `fork()` 中，除复制页表外，还需把父进程的 VMA 表浅拷贝到子进程，并对每个 `vma->f` 执行 `filedup()` 增加引用计数。
  - 子进程的页故障处理可以独立分配并从文件读入，不要求与父共享物理页。
- 测试与验证顺序
  - 先让 `mmaptest` 编译运行（`mmap`/`munmap` 先返回错误），确认第一个 `mmap` 调用失败。
  - 实现 `mmap` + VMA 后，运行 `mmaptest`，应该在首次访问映射内存时因缺页而崩溃。
  - 加入缺页处理逻辑后，`mmaptest` 能走到 `munmap`。
  - 实现 `munmap` 写回/解除映射后，`mmaptest` 前面的用例通过。
  - 实现 `exit` 写回与 `fork` 继承后，`mmaptest` 全部通过。
  - 最后运行 `usertests -q`，确保不破坏其他功能。

- 关键文件与位置：
  - `kernel/syscall.h`：添加 `SYS_mmap`、`SYS_munmap`。
  - `kernel/syscall.c`：系统调用分发表挂接 `sys_mmap`、`sys_munmap`。
  - `kernel/sysfile.c` 或 `kernel/sysproc.c`：实现 `sys_mmap`、`sys_munmap`，用 `argfd` 拿到 `struct file*`。
  - `kernel/proc.h`：定义 `struct vma` 并在 `struct proc` 中加入 VMA 数组。
  - `kernel/trap.c`：在 `usertrap()` 的页故障路径里处理 VMA 懒加载。
  - `kernel/proc.c`：在 `exit()` 和 `fork()` 中处理 VMA 写回与继承。
  - `user/usys.pl`、`user/user.h`：用户态系统调用声明与封装。
  - `Makefile`：在 `UPROGS` 增加 `_mmaptest`。

- 实现细节与注意点：
  - 地址与长度页对齐：`addr` 取 `PGROUNDUP`；计算页数要考虑 `len` 尾页的部分。
  - 权限位与 PTE：根据 `prot` 设置 `PTE_R`/`PTE_W`；始终设置 `PTE_U|PTE_V`。
  - 文件 I/O 加锁与日志：参照 `filewrite()` 的 `begin_op/end_op` 与 `ilock/iunlock`，避免破坏文件系统一致性。
  - 不同进程 `MAP_SHARED` 的页不要求物理共享；每进程独立加载即可。
  - 与进程普通地址空间（`p->sz`）避免重叠；不要把 `mmap` 的区间算进 `p->sz`，以免影响 `sbrk` 等。
  - 释放页使用 `uvmunmap(..., do_free=1)`；映射建立用 `mappages`。

## 实验过程

### 基本功能实现

- 添加 `SYS_mmap`/`SYS_munmap` 编号与用户态封装
- 在系统调用表注册 `sys_mmap`/`sys_munmap`
- 在 `proc.h` 定义 VMA 并加入到 `struct proc`
- 实现 `sys_mmap`/`sys_munmap`（建立/清理 VMA，返回地址/错误）
- 在 `usertrap` 缺页路径中实现懒加载映射
- 在 `exit()`/`fork()` 处理 VMA 写回与继承

### 排查 dirty mmap 相关问题处理

- 失败用例场景：在 mmap_test 中，映射 3 页 MAP_SHARED ，对第 1 页写 'B' 、第 2 页写 'C' ，随后 munmap 前两页。之后以只读方式打开文件并读两次：
  - 第一次读应返回 PGSIZE ，内容为 'B' 。
  - 第二次读应返回剩余的半页 PGSIZE/2 ，内容为 'C' 。

- 失败原因：写回逻辑在 sys_munmap 和 kexit 中按映射长度 v->len 写满整页，导致文件被扩展到 2 页而不是原来的 1.5 页。于是第二次 read(fd, buf, PGSIZE) 返回 PGSIZE 而不是预期的 PGSIZE/2 ，触发 “dirty read #2”。

- 限制写回长度不超过文件原始大小 ip->size ，避免扩展文件：
  - kernel/sysfile.c 在 sys_munmap 写回环节使用 ip->size 截断：
    - 计算 n = min(PGSIZE, ip->size - off) ；
    - 若 n <= 0 则跳过写回。
  - kernel/proc.c 在 kexit 写回环节同样使用 ip->size 截断并跳过非正写入。

- 为什么这样修复：
  - 测试期望写回只覆盖文件中已有的字节，不应通过 MAP_SHARED 扩展文件大小。通过 ip->size 截断，第二次读就会返回 PGSIZE/2 ，与测试一致。
  - 保留了页内零填充策略： vmfault 在读文件前对页做 memset ，读取超出 EOF 部分保持为 0，不影响该用例。

### fork 细节修复

- 触发背景：fork 后父子进程需继承 mmap 映射并保持语义一致。在实验中暴露出两类问题：
  - 只读共享误写回：对 `MAP_SHARED` 的只读映射在 `munmap/exit` 路径上被错误写回，导致文件内容被零填充覆盖。
  - 缩小 VMA 偏移错误：从 VMA 头部解除映射后，写回逻辑仍按原始起点计算偏移，导致写回落在文件开头（`mmaptest` 报告首字节错误）。

- 修复要点：
  - VMA 继承：在 `kfork()` 中浅拷贝父进程的 `vmas[]` 和 `mmap_base`，对每个 `vma->f` 执行 `filedup()`，保持文件引用计数一致；结构体拷贝确保新字段 `vma->foff` 一并继承。
  - 独立懒加载：子进程不复制父的 mmap 物理页；页错误由 `vmfault()` 按需分配物理页并读取文件，权限依据 `prot` 设置（`PTE_R/PTE_W`）。
  - 写回条件：仅在“共享且可写”（`MAP_SHARED` 且 `PROT_WRITE`）时执行写回，避免只读映射误写回。
  - 写回与读取偏移：统一改为 `file_offset = vma->foff + (PGROUNDDOWN(va) - vma->addr)`；页级写回使用 `off = vma->foff + (a - v->addr)`，修正缩小 VMA 后的偏移错误。
  - VMA 缩小同步：当“从头部解除映射”时，同时推进 `vma->foff += mlen`，确保后续 `vmfault/readi/writei` 的偏移与新起点一致。
  - TLB 刷新：在 `sys_munmap()` 和 `kexit()` 解除映射后调用 `sfence_vma()`，确保父/子进程不会继续命中旧的 TLB 条目（避免“munmap prevents access”不生效）。
  - 语义说明：本实验不要求跨进程物理页实时共享；`MAP_SHARED` 的可见性通过文件层的写回体现，满足 `mmaptest` 的用例即可。

- 关键代码位置：
  - `kernel/proc.c:kfork()`：继承 `vmas[]`、复制 `mmap_base`、对每个 `vma->f` 执行 `filedup()`。
  - `kernel/vm.c:vmfault()`：页错误时使用 `vma->foff` 计算文件偏移并装载页面。
  - `kernel/sysfile.c:sys_munmap()` 与 `kernel/proc.c:kexit()`：写回仅在共享可写时进行，使用 `vma->foff+页内偏移`，解除映射后执行 `sfence_vma()`。

- 采用添加细粒度调试代码的方式，查看读写细节：![debug.png](debug.png)

- 验证要点：
  - `MAP_PRIVATE`：子进程的写不落盘，父进程不受影响；解除映射后访问应触发页错误。
  - `MAP_SHARED`：子进程的写在 `munmap()` 或退出时写回到文件；父进程再次访问或重新映射后能观察到更新；跨页与部分解除映射边界正确。
  - 只读共享映射不写回；缩小映射后文件首字节不再被错误覆盖。

### munmap prevents access 细节修复

- 问题表现：在解除映射后，用户态仍能读/写原地址范围，或偶发读取到旧数据，`mmaptest` 的“munmap prevents access”用例失败。

- 根因分析：
  - 解除映射后页表已删除，但处理器的 TLB 保留了旧的（已无效）PTE 条目，导致访问命中 TLB、绕过页表检查继续读写。
  - 进程退出时解除所有 VMA 映射后未刷新 TLB，子流程或调度回到同一 hart 时仍可能命中旧条目。

- 修复方案：
  - 在 `sys_munmap()` 中：完成 `uvmunmap(p->pagetable, start, npages, 1)` 后立刻调用 `sfence_vma()` 刷新当前 hart 的 TLB，清除已解除映射范围的旧条目。
  - 在 `kexit()` 中：遍历所有 VMA 执行写回与解除映射后，统一调用一次 `sfence_vma()`，确保进程退出后不再能访问被解除的范围。
  - 写回条件与权限协同：仅在“共享且可写”（`MAP_SHARED` 且 `PROT_WRITE`）时执行写回；只读映射不写回，避免误覆盖文件内容。

- 关键代码位置：
  - `kernel/sysfile.c:sys_munmap()`：
    - 解除映射：`uvmunmap(p->pagetable, start, mlen/PGSIZE, 1)`。
    - 刷新 TLB：`sfence_vma()`（RISC‑V 指令 `sfence.vma`）。
  - `kernel/proc.c:kexit()`：
    - 逐 VMA 写回与解除映射：`uvmunmap(...)`。
    - 全局刷新：在所有 VMA 处理后调用 `sfence_vma()`。

- 验证方法：
  - 运行 `mmaptest`，关注“munmap prevents access”子测：解除映射后对同一地址的读/写应触发页故障。
  - 在 `LAB_PGTBL` 打印下应观察到：
    - `munmap` 打印了解除范围的页写回（仅共享可写）与 `uvmunmap` 执行；之后无对只读映射的写回日志。
    - 再次访问解除范围时，`usertrap` 报告 `scause=13/15`（load/store page fault），`vmfault` 对该地址不再命中任何 VMA，返回 0，访问失败。
  - 交叉验证：`usertests -q` 无回归；`MAP_PRIVATE` 的解除映射后访问同样被拒绝。

- 注意事项：
  - RISC‑V 的 `sfence.vma` 仅影响当前 hart 的 TLB；xv6 教学环境通常单核，足够满足本实验需求。多核情况下需要在相关 hart 上执行或在上下文切换中统一刷新策略。
  - 解除映射后的页错误路径保持原有策略：未命中 VMA 则视为非法访问并终止当前用户态操作（或杀死进程），与测试预期一致。
