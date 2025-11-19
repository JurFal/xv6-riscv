# Advanced Slab Allocator Test Guide

## 概述

这是一个重新设计的slab分配器测试程序，使用了mock技术和shadow memory来精确测试slab分配器的功能。相比原版测试，新版本提供了更精确的内存追踪、泄漏检测和碎片分析功能。

## 核心特性

### 1. Mock Memory Management
- **Mock kalloc/kfree**: 模拟内核页面分配器，避免依赖真实的内核内存管理
- **精确时间追踪**: 每次分配/释放操作都有时间戳记录
- **内存使用统计**: 实时追踪总内存使用量和峰值使用量
- **页面状态管理**: 追踪每个页面的分配状态、引用计数和生命周期

### 2. Shadow Memory Tracking
- **分配追踪**: 记录每个malloc/free操作的详细信息
- **内存泄漏检测**: 自动识别未释放的内存块
- **碎片分析**: 计算内存碎片化程度
- **缓存分类**: 按不同大小类别追踪分配情况

### 3. 高级测试功能
- **模式验证**: 写入和验证内存模式，确保数据完整性
- **性能分析**: 详细的时间和成功率统计
- **压力测试**: 多轮分配/释放循环测试
- **碎片抗性测试**: 测试分配器对内存碎片的处理能力

## 测试用例详解

### 1. Basic Allocation Test (`basic`)
```bash
./slabtest basic
```
- **目的**: 测试基本的分配和释放功能
- **操作**: 分配50个64字节的内存块，写入模式，验证数据，然后释放
- **验证**: 数据完整性、内存泄漏检测

### 2. Size Classes Test (`sizes`)
```bash
./slabtest sizes
```
- **目的**: 测试不同大小类别的分配效率
- **操作**: 测试8B到4KB的10个不同大小类别，每个类别分配10个对象
- **验证**: 各种大小的分配成功率和性能

### 3. Fragmentation Resistance Test (`frag`)
```bash
./slabtest frag
```
- **目的**: 测试分配器的碎片抗性
- **操作**: 
  1. 交替分配32B和128B的对象
  2. 释放一半对象创建内存洞
  3. 尝试在洞中分配64B对象
- **验证**: 碎片化程度和重用能力

### 4. Memory Leak Detection Test (`leak`)
```bash
./slabtest leak
```
- **目的**: 测试内存泄漏检测功能
- **操作**: 故意不释放部分内存，测试shadow memory的泄漏检测
- **验证**: 准确识别泄漏的内存块数量

### 5. Stress Allocation Test (`stress`)
```bash
./slabtest stress
```
- **目的**: 压力测试分配器的稳定性和性能
- **操作**: 多轮大量分配/释放循环，使用不同大小的对象
- **验证**: 高负载下的稳定性和性能表现

## 测试指标说明

### 基本统计
- **Duration**: 测试执行时间（ticks）
- **Total allocations**: 总分配次数
- **Successful allocations**: 成功分配次数
- **Failed allocations**: 失败分配次数
- **Success rate**: 分配成功率百分比

### 高级指标
- **Memory leaks**: 检测到的内存泄漏数量
- **Fragmentation score**: 碎片化评分（0-100，越低越好）
- **Peak memory usage**: 峰值内存使用量
- **Average allocation time**: 平均分配时间

### 碎片化评分算法
```
fragmentation_score = (allocated_pages * 100) / (active_objects + 1)
```
- 分数越低表示内存利用率越高
- 理想情况下，多个小对象应该共享同一页面

## 编译和运行

### 编译测试程序
```bash
make clean
make
```

### 运行完整测试套件
```bash
./slabtest
```

### 运行单个测试
```bash
./slabtest basic    # 基本分配测试
./slabtest sizes    # 大小类别测试
./slabtest frag     # 碎片抗性测试
./slabtest leak     # 内存泄漏测试
./slabtest stress   # 压力测试
```

## 测试结果分析

### 成功标准
一个测试被认为是**PASS**当且仅当：
- `failed_allocs == 0` (没有分配失败)
- `memory_leaks == 0` (没有内存泄漏)

### 性能基准
- **分配成功率**: 应该达到100%
- **碎片化评分**: 应该低于50（对于小对象分配）
- **内存泄漏**: 应该为0
- **平均分配时间**: 应该保持稳定

### 常见问题诊断

#### 1. 分配失败率高
- 可能原因：mock页面池耗尽
- 解决方案：增加`MAX_PAGES`常量

#### 2. 碎片化评分高
- 可能原因：slab分配器效率低下
- 分析：检查是否正确实现了大小类别缓存

#### 3. 内存泄漏
- 可能原因：free函数实现有问题
- 分析：检查shadow memory追踪日志

#### 4. 性能下降
- 可能原因：分配器算法效率问题
- 分析：比较不同测试的平均分配时间

## Mock系统设计

### Mock Page Management
```c
struct mock_page {
  void *addr;           // 页面地址
  int allocated;        // 分配状态
  int ref_count;        // 引用计数
  uint64 alloc_time;    // 分配时间
  uint64 free_time;     // 释放时间
};
```

### Shadow Memory Tracking
```c
struct shadow_alloc {
  void *ptr;            // 分配的指针
  uint size;            // 分配大小
  uint64 alloc_time;    // 分配时间
  int cache_id;         // 缓存ID
  int active;           // 活跃状态
};
```

## 扩展功能

### 添加新测试用例
1. 在`slabtest.c`中添加新的测试函数
2. 在`main`函数中添加命令行参数处理
3. 使用shadow memory API追踪内存操作

### 自定义Mock行为
- 修改`mock_kalloc`和`mock_kfree`函数
- 调整`MAX_PAGES`和`MAX_ALLOCS`常量
- 添加更多的统计信息收集

### 调试支持
- 启用详细的shadow memory日志
- 添加内存状态转储功能
- 实现分配历史追踪

## 与原版测试的对比

| 特性 | 原版测试 | 高级测试 |
|------|----------|----------|
| 内存追踪 | 基本 | 精确的shadow memory |
| 泄漏检测 | 无 | 自动检测 |
| 碎片分析 | 无 | 量化评分 |
| Mock支持 | 无 | 完整的mock系统 |
| 数据验证 | 基本 | 模式验证 |
| 性能分析 | 简单 | 详细统计 |

这个高级测试系统为slab分配器提供了全面而精确的测试能力，能够帮助开发者更好地理解和优化内存分配器的性能。