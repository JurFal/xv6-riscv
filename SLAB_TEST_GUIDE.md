# Slab 内存分配器性能测试指南

## 概述

本测试套件用于量化评估 xv6 中 Slab 内存分配器相对于默认页级分配器的性能提升。测试程序 `slabtest` 可以在实现 Slab 分配器前后运行，提供客观的性能对比数据。

## 测试程序功能

### 测试项目

1. **顺序分配测试 (Sequential Alloc/Free)**
   - 顺序分配固定数量的内存块
   - 按分配顺序释放所有内存块
   - 测量总时间和平均分配时间

2. **随机分配测试 (Random Alloc/Free)**
   - 随机模式的内存分配和释放
   - 模拟真实应用的内存使用模式
   - 测试分配器的碎片处理能力

3. **并发分配测试 (Concurrent Alloc/Free)**
   - 多进程并发进行内存分配
   - 测试分配器的并发性能和锁争用
   - 验证多线程环境下的稳定性

4. **混合大小测试 (Mixed Size Alloc/Free)**
   - 分配不同大小的内存块 (16B-1024B)
   - 测试分配器对不同对象大小的适应性
   - 评估 Slab 分配器的大小类优化效果

5. **碎片化测试 (Fragmentation Test)**
   - 创建内存碎片后尝试分配较大块
   - 测试分配器的碎片整理能力
   - 评估内存利用率

### 测试指标

- **执行时间**: 每个测试的总执行时间（以系统 tick 为单位）
- **分配次数**: 成功分配的内存块数量
- **失败次数**: 分配失败的次数
- **成功率**: 分配成功率百分比
- **平均分配时间**: 单次分配的平均时间

## 使用方法

### 1. 编译和运行

```bash
# 编译 xv6（包含测试程序）
make

# 启动 xv6
make qemu

# 在 xv6 shell 中运行完整测试
$ slabtest

# 运行快速测试
$ slabtest quick
```

### 2. 测试流程

#### 阶段一：基线测试（默认分配器）

1. 在未实现 Slab 分配器的 xv6 上运行测试
2. 记录所有测试项目的性能数据
3. 保存结果作为基线参考

```bash
$ slabtest > baseline_results.txt
```

#### 阶段二：Slab 分配器测试

1. 实现 Slab 分配器
2. 修改内核使用 Slab 分配器
3. 重新编译并运行相同测试
4. 对比性能提升

```bash
$ slabtest > slab_results.txt
```

### 3. 结果分析

#### 预期性能提升

**小对象分配 (64-256 字节)**:
- 分配时间减少 50-80%
- 并发性能提升 30-60%
- 碎片化显著减少

**混合大小分配**:
- 整体性能提升 40-70%
- 内存利用率提升 20-40%

**并发场景**:
- 锁争用减少
- 吞吐量提升 2-4 倍

#### 性能对比示例

```
=== 基线测试结果（默认分配器）===
Test: Sequential Alloc/Free
  Duration: 150 ticks
  Allocations: 1000
  Avg time per alloc: 0.15 ticks

=== Slab 分配器测试结果 ===
Test: Sequential Alloc/Free
  Duration: 45 ticks
  Allocations: 1000
  Avg time per alloc: 0.045 ticks

性能提升: 70% (150 -> 45 ticks)
```

## 测试配置

### 可调参数

在 `slabtest.c` 中可以修改以下参数：

```c
#define MAX_ALLOCS 1000        // 每个测试的分配次数
#define SMALL_SIZE 64          // 小对象大小
#define MEDIUM_SIZE 256        // 中等对象大小
#define LARGE_SIZE 1024        // 大对象大小
```

### 测试环境要求

- xv6 操作系统
- 至少 64MB 物理内存
- 支持多进程并发

## 故障排除

### 常见问题

1. **编译错误**
   - 确保 `slabtest.c` 在 `user/` 目录下
   - 检查 `Makefile` 中是否正确添加了 `$U/_slabtest`

2. **运行时错误**
   - 内存不足：减少 `MAX_ALLOCS` 值
   - 分配失败：检查内核内存分配器实现

3. **性能异常**
   - 如果 Slab 分配器性能更差，检查实现是否正确
   - 确保正确初始化了 Slab 分配器

### 调试建议

1. **启用详细输出**
   - 在测试程序中添加更多调试信息
   - 使用 `printf` 跟踪分配过程

2. **分步测试**
   - 先运行 `slabtest quick` 进行快速验证
   - 逐个运行各项测试以定位问题

3. **内存监控**
   - 监控系统内存使用情况
   - 检查是否存在内存泄漏

## 验收标准

### 功能要求

- [ ] 所有测试项目运行无崩溃
- [ ] 分配成功率 > 95%
- [ ] 并发测试稳定运行
- [ ] 内存正确释放（无泄漏）

### 性能要求

- [ ] 小对象分配性能提升 > 50%
- [ ] 并发性能提升 > 30%
- [ ] 混合大小分配性能提升 > 40%
- [ ] 碎片化测试通过率 > 90%

## 扩展测试

### 自定义测试

可以基于现有框架添加新的测试用例：

```c
void test_custom(struct test_result *result) {
    // 自定义测试逻辑
    strcpy(result->name, "Custom Test");
    result->start_time = get_time();
    
    // 执行测试...
    
    result->end_time = get_time();
    result->duration = result->end_time - result->start_time;
}
```

### 压力测试

对于长时间运行的压力测试，可以修改参数：

```c
#define MAX_ALLOCS 10000       // 增加分配次数
#define TEST_ITERATIONS 100    // 增加测试轮次
```

## 总结

本测试套件提供了全面的内存分配器性能评估工具，能够客观量化 Slab 分配器的性能优势。通过对比测试，可以验证 Slab 分配器在小对象分配、并发性能和内存利用率方面的显著改进。