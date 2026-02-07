# How to Copy Memory? Coordinated Asynchronous Copy as a First-Class OS Service 论文（SOSP 2025）阅读与复现尝试报告

## 论文阅读报告

### 引言

- **问题**：内存复制在现代系统中仍是关键性能瓶颈，尤其在系统调用、IPC、网络栈等场景中。
- **现有方法不足**：
  - 硬件加速（如SIMD、DMA）无法被内核与用户态同时充分利用。
  - 零复制（zero-copy）存在页面对齐、安全漏洞（TOCTTOU）、无法支持多副本等限制。

- **核心观点**：应将复制视为**操作系统一级服务**，提供异步、协调、全局优化的复制能力。有三个关键原因支持这样的观点：
  1. 复制与系统服务应当**异步**。研究发现，存在用于掩盖复制延迟且不阻塞应用执行的时间窗口。
  2. 复制操作应当利用好**硬件**加速（如SIMD、DMA），以最大程度地提高复制效率。
  3. 复制操作应当**全局优化**，以避免冗余复制并计划重点优先级。
- **形象化表述**：利用时间空隙和异步服务特性，在应用的计算这一“主线”操作之外开一个并行的复制“支线”，赶在计算开始后和需要使用复制数据之前的间隙完成复制，最大化利用时间加速各类操作。

### 系统分析

- **复制开销仍显著**：在 Redis、Proxy 等应用中，复制占CPU周期的66.2%。
- **复制分类**：
  - **边界内复制**：同一地址空间内的复制，常因内存组织、语义桥接、多副本需求而存在。
  - **边界间复制**：跨特权级或地址空间的复制，如系统调用、IPC，优化难度更大。
- **现有优化方法对比**：零复制、硬件加速、页面重映射等各有局限，无法覆盖中小规模复制或跨特权场景。

### 复制作为操作系统服务

- **洞察1**：复制应作为系统服务，以全局视角调度和优化复制任务，这样做有两个理由：利用硬件；复制时的全局观。
- **洞察2**：存在“复制-使用窗口”（Copy-Use window），可用于隐藏复制延迟。这是通过量化计算得出的。
- 根据两个洞察，提出了 **Copier**，一个用于异步复制的系统服务。它的设计目标：
  - 异步复制，支持重叠执行。
  - 充分利用硬件（SIMD + DMA）。
  - 全局优化，消除冗余复制。

### Copier 设计

#### 基于队列的抽象

- **核心思想**：流水线化的队列式抽象。
- **API**：`amemcpy()`（异步复制） + `csync()`（同步点）。
- **队列**：复制队列、同步队列、处理队列，支持细粒度状态更新和乱序执行。
  - 复制队列（Copy Queue）
    - 将大复制任务分成固定大小的段（segment）
    - 每个段在描述符中有一个状态位
    - 客户端可以轮询描述符了解进度
  - 同步队列（Sync Queue）
    - 目的：解决FIFO队列的队头阻塞问题
    - 机制：当客户端发现所需数据段未就绪时，提交Sync Task
    - 效果：提升相关复制任务的优先级，实现乱序执行
  - 处理队列（Handler Queue）
    - 在复制任务中嵌入处理函数
    - KFUNC：内核函数，Copier直接调用
    - UFUNC：用户函数，Copier提交到Handler Queue，由客户端库执行

> [!note]
> 作者在附录通过语意转换形式化证明了异步复制与同步复制的等价性。

#### 依赖跟踪

- **顺序依赖**：通过系统事件（如系统调用进入/返回）作为屏障，跨队列同步。
- **数据依赖**：通过内存区域重叠检测，支持复制吸收和任务重排。

#### 硬件协调与任务捎带调度

- **混合调度**：将DMA任务捎带在AVX任务上执行，避免CPU等待。
- **地址转换缓存（ATCache）**：缓存虚拟-物理地址映射，提升DMA效率。

#### 复制吸收（Copy Absorption）

- **消除冗余复制**：通过全局视图合并多个复制任务（如 `A→B→C` 合并为 `A→C`）。
- **惰性复制（Lazy Copy）**：标记低优先级复制任务，仅在必要时执行。

#### 多客户端资源调度与隔离

- **Copier线程**：基于io_uring实现高效轮询。
- **cgroup扩展**：以复制长度为资源单位进行调度与隔离。
- **主动故障处理**：提前触发和处理页错误，避免在复制上下文中处理异常。

### Copier 实践

- **工具链**：
  - `libCopier`：提供高/低级API。
  - `CopierSanitizer`：用于检测遗漏的 `csync`。
  - `CopierGen`：基于LLVM/MLIR的自动化移植工具。
- **Linux案例**：优化网络栈、CoW故障处理、Binder IPC。
- **HarmonyOS 5.0 案例**：应用于视频编解码场景，降低延迟。

### 实验评估

- **微基准测试**：
  - 复制吞吐提升最高达158%（相比内核ERMS）。
  - 系统调用延迟降低7%~92%。
- **真实应用测试**：
  - Redis：延迟降低43.4%，吞吐提升50%。
  - TinyProxy：吞吐提升32.3%。
  - Protobuf、OpenSSL等也有显著提升。
- **智能手机环境**：视频解码延迟降低10%。

### 讨论与展望

- **硬件原语支持**：未来可将Copier功能集成到CPU或内存控制器中。
- **更多应用场景**：文件I/O、内存分级、设备虚拟化等。
- **安全性**：Copier避免了零复制的TOCTTOU风险。

### 相关工作

- 异步系统调用（FlexSC、io_uring）
- 零复制优化（IX、Cornflakes）
- 硬件辅助复制（RowClone、DSA）

### 结论

- Copier是首个将异步内存复制作为独立OS服务的工作，通过全局调度、硬件协同、复制吸收等机制，显著提升了系统整体复制效率。

## 复现实验

### 实验简介

实现了一个**异步复制库**，模拟Copier的队列调度和异步执行机制，验证“复制-使用窗口”的可行性，集成到内核中。

实验目的：验证“**异步复制作为OS服务**”的核心思想，而非完整性能优化。

### 复现思路

#### 扩展xv6系统调用

```c
// 新增系统调用
int amemcpy(void *dst, void *src, int len);
int csync(void *addr, int len);
int copier_create_queue(void);
```

#### 实现Copier内核服务

```c
struct copier_task {
    void *src, *dst;
    int len;
    int status; // 分段状态位图
    struct spinlock lock;
};

struct copier_queue {
    struct copier_task tasks[N];
    int head, tail;
};

// 内核线程函数
void copier_thread(void) {
    while (1) {
        // 轮询队列，执行复制
        // 更新任务状态
        // 处理依赖关系
    }
}
```

#### 实现分段复制与状态更新

- 将每个复制任务分成固定大小的段（如256字节）。
- 使用位图跟踪每个段的完成状态。
- 允许`csync`等待特定段的完成。

#### 实现简单依赖跟踪

- 在系统调用入口/出口插入屏障任务。
- 检测内存重叠，建立任务依赖图。

#### 测试用例

- 编写测试程序：模拟Redis的SET/GET操作中的复制链。
- 对比同步`memcpy`与异步`amemcpy`的延迟。

### 测试结果

对于 **MB 级别**的复制任务，异步复制提升效果显著，对 8MB 级别任务时间缩短 7 倍；对 KB 级别复制任务提升效果不显著。

详细数据：

```text
Starting Multi-Size Async Copy Benchmark...
--- Testing 4KB (Size: 4096 bytes) ---
Sync copy time: 1 ticks
Async copy time: 1 ticks
Async copy verification: PASSED
RESULT: Async SLOWER/EQUAL (diff: 0 ticks)

--- Testing 64KB (Size: 65536 bytes) ---
Sync copy time: 0 ticks
Async copy time: 1 ticks
Async copy verification: PASSED
RESULT: Async SLOWER/EQUAL (diff: 1 ticks)

--- Testing 256KB (Size: 262144 bytes) ---
Sync copy time: 1 ticks
Async copy time: 1 ticks
Async copy verification: PASSED
RESULT: Async SLOWER/EQUAL (diff: 0 ticks)

--- Testing 512KB (Size: 524288 bytes) ---
Sync copy time: 1 ticks
Async copy time: 1 ticks
Async copy verification: PASSED
RESULT: Async SLOWER/EQUAL (diff: 0 ticks)

--- Testing 1MB (Size: 1048576 bytes) ---
Sync copy time: 4 ticks
Async copy time: 1 ticks
Async copy verification: PASSED
RESULT: Async FASTER (diff: 3 ticks)

--- Testing 2MB (Size: 2097152 bytes) ---
Sync copy time: 2 ticks
Async copy time: 2 ticks
Async copy verification: PASSED
RESULT: Async SLOWER/EQUAL (diff: 0 ticks)

--- Testing 4MB (Size: 4194304 bytes) ---
Sync copy time: 4 ticks
Async copy time: 3 ticks
Async copy verification: PASSED
RESULT: Async FASTER (diff: 1 ticks)

--- Testing 8MB (Size: 8388608 bytes) ---
Sync copy time: 35 ticks
Async copy time: 5 ticks
Async copy verification: PASSED
RESULT: Async FASTER (diff: 30 ticks)

Benchmark suite completed.
```
