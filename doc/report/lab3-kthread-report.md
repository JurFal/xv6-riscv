# Lab 3：kthread 实验报告

## kthread 机制实现

- 核心接口：`kthread_create(void (*func)(void *), void *arg, char *name)` 在 `kernel/proc.c` 中实现，基于现有进程控制块（`struct proc`）创建内核线程。
- 资源分配：复用 `allocproc()` 以分配并初始化 `proc` 结构与内核栈、页表及上下文。`allocproc()` 返回时持有 `p->lock`，随后在 `kthread_create` 内完成如下设置：
  - 设置内核线程入口 `p->context` 与栈指针。
  - 将 `p->is_kthread` 置位，并设置线程名。
  - 将 `p->state` 置为 `RUNNABLE` 并释放 `p->lock`，避免双重加锁导致的 `panic: acquire`。
- 运行机制：调度器将其与普通进程一致调度，入口函数负责执行传入的 `func(arg)`。为避免锁重入，入口函数在开始时会释放不必要的锁（例如 `p->lock`）。
- 创建时机：在 `kernel/main.c` 中，在用户态初始进程建立后（`userinit()`）创建内核线程，如 `khugepaged` 背景线程：`kthread_create(khugepaged_main, 0, "khugepaged");`。
- 调试修正：为定位早期 `panic: acquire`，在 `spinlock.c` 增加调试输出，最终发现 `kthread_create` 末尾对 `p->lock` 的重复获取；现已改为在 `allocproc()` 持锁状态下直接设置为 `RUNNABLE` 并释放锁，不再重复 `acquire`。

## khugepaged 实现

- 功能目标：在后台扫描各进程页表，检测连续 512 个 4KB 页是否满足合并条件，将其折叠为一个 2MB 超页以提升 TLB 命中率与内存访问效率。
- 扫描流程（位于 `kernel/kpage.c`）：
  - 遍历进程的 L2 根页表，跳过叶子（1GB 映射），进入 L1（每项覆盖 2MB）。
  - 对每个 L1 项，若指向 L0 表，则检查该 L0 表的 512 项是否全部有效且为叶子（4KB 映射）。
  - 满足条件时，申请一个 2MB 超页（现已通过 Buddy Allocator 背书），将 512×4KB 内容拷贝至该超页，并在 L1 处写入叶子映射（2MB）。
  - 刷新 TLB（`sfence_vma()`），释放旧的 512×4KB 页与 L0 页表，并打印折叠日志。
- 同步约束：在扫描过程中持有目标进程的 `p->lock`，确保进程状态稳定；对 `ticks` 使用睡眠等待控制后台线程的周期。
- 接口适配：原先使用 `superalloc/superfree` 获取/释放 2MB 超页；在本次集成后，`superalloc/superfree` 已改为 Buddy 的 `order=9` 封装，同时也提供 `kalloc_huge/kfree_huge` 以统一语义。

## buddy allocator 机制实现

- 设计目标：维持原有 `slab/uvmalloc/uvmcopy` 架构不变，替换底层物理页分配器为 Buddy Allocator，以同时支持 4KB 基页与 2MB 超页的高效分配与合并。
- 关键数据结构（`kernel/kalloc.c`）：
  - `MAX_ORDER=9`：`order=0` 表示 4KB，`order=9` 表示 2MB。
  - `buddy.free[0..9]`：每一阶的空闲链表，统一由 `buddy.lock` 保护（简单且避免锁序复杂性）。
- 初始化与切分：
  - `kinit()` 初始化 Buddy 并清空各阶链表。
  - `freerange(end, PHYSTOP)` 从 `PGROUNDUP(end)` 起将物理内存按“尽可能大的、且对齐的”块切分，逐块挂入对应阶的空闲链表；对齐不满足时回退到 4KB。
- 分配与释放：
  - `kalloc()`：寻找可用块阶；若高于 `order=0` 则逐级拆分，另一半块挂回低一阶链表，最终返回 4KB 页（维持原有 `memset(...,5)` 习惯）。
  - `kfree(pa)`：校验对齐与范围后，执行伙伴合并，从 `order=0` 开始向上查找并合并相邻伙伴，直至无法合并或达到最大阶；合并完成后挂回对应阶链表（维持原有 `memset(...,1)` 习惯）。
  - `superalloc()`/`superfree()`：分别对应 Buddy 的 `order=9` 分配与释放；必要时从更高阶拆分到 `order=9`。
  - 新增 `kalloc_huge()`/`kfree_huge()`（`LAB_PGTBL` 下声明）：语义上明确“超页”分配，内部调用上述封装。
- 兼容性与影响：
  - `slab` 的对象级分配不受影响；其底层页获取仍通过 `kalloc/kfree`。
  - `uvmalloc/uvmcopy/uvmunmap` 维持现有代码路径；当需要 2MB 超页时通过超页接口获取。
  - 与早期的“超页池”方案相比，Buddy 能在碎片化场景下更好地合并与拆分，减少浪费。

## 实验过程

- 首先实现了 `kthread` 的相关小修改。
- 发现此实验可以使用到之前实验中的 Slab Allocator，所以开始研究如何合并所有实验的代码，成功并运行出所有先前的测试结果。
- 合并完成后，询问 AI，直接借用之前 uvmalloc 实验中的超页机制实现 `khugepaged` 背景线程。修复了一个多重获取锁的 panic 问题后，执行成功。但是没发现需要使用 Buddy Allocator。
- 后来改善了 `khugepaged` 的实现，将超页分配从 `superalloc/superfree` 改为使用 Buddy Allocator 的 `kalloc_huge/kfree_huge`，并成功运行。
