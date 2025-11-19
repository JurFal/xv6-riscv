# xv6 Slab Allocator Tests

这个文档描述了xv6 slab分配器的测试套件，包括对比测试、统计接口和并发测试。

## 测试概述

### Milestone C 功能实现

1. **空slab回收机制** - 支持将空slab归还给底层页面
2. **统计接口** - 提供详细的内存使用统计和碎片率分析
3. **性能对比** - slab分配器与xv6 kalloc的性能对比测试
4. **并发测试** - 多线程环境下的稳定性测试

## 测试列表

### 基础测试 (1-6)
- **Test 1**: 基础分配测试
- **Test 2**: 大小类别测试
- **Test 3**: 压力测试
- **Test 4**: 边界情况测试
- **Test 6**: 增强压力测试（带影子内存检查）

### 新增测试 (7-12)
- **Test 7**: 统计和碎片分析测试
- **Test 8**: 性能对比测试（Slab vs Kalloc）
- **Test 9**: 并发分配测试（模拟多线程）
- **Test 10**: 内存回收测试
- **Test 11**: 进程/文件操作压力测试
- **Test 12**: 综合系统测试

## 运行测试

### 方法1: 使用测试脚本（推荐）

```bash
# 交互式运行
./run_slab_tests.sh

# 运行所有测试
./run_slab_tests.sh all

# 运行特定测试
./run_slab_tests.sh 8  # 运行性能对比测试

# 清理测试输出文件
./run_slab_tests.sh clean
```

### 方法2: 手动运行

```bash
# 编译并运行特定测试
make clean
make qemu CPUS=1 TEST_TYPE=8  # 运行测试8
```

## 统计接口

### 每个cache的统计信息
- `objsize`: 对象大小
- `nr_slabs`: slab数量
- `nr_objs`: 对象总数
- `nr_free`: 空闲对象数
- `pages_in_use`: 使用的页面数

### 碎片率计算
支持两种碎片率计算方式：
1. `allocated_bytes / (pages_in_use * PGSIZE)`
2. `1 - in_use_bytes / total_bytes`

### 使用统计接口

```c
// 获取全局统计
struct slab_global_stats global_stats;
slab_get_global_stats(&global_stats);

// 获取特定cache统计
struct slab_cache_stats cache_stats;
slab_get_cache_stats(cache, &cache_stats);

// 打印所有统计信息
slab_print_stats();

// 打印碎片分析报告
slab_print_fragmentation_report();
```

## 性能对比测试

Test 8 提供了slab分配器与xv6 kalloc的详细性能对比：

### 测试指标
- **分配成功率**: 成功分配的对象数量
- **时间性能**: 分配/释放操作的时间开销
- **内存效率**: 实际使用的内存页面数
- **吞吐量**: 单位时间内的分配次数

### 对比结果
- **速度比**: slab vs kalloc的速度对比
- **内存效率**: 内存使用效率对比
- **内存节省**: slab相对于kalloc的内存节省
- **每对象浪费**: 平均每个对象的内存浪费

## 并发测试

Test 9 模拟多线程环境下的并发分配：

### 测试场景
- 模拟3个线程同时进行不同大小的对象分配
- 交错的分配和释放操作
- 验证无崩溃、无内存泄漏

### 验证方法
- 检查分配错误数量
- 验证最终统计数据归零
- 确保内存正确回收

## 压力测试

Test 11 模拟真实系统负载：

### 模拟场景
- 大量进程创建/销毁
- 文件操作压力
- 各种大小的内核结构分配

### 测试目标
- 验证系统稳定性
- 测试内存回收机制
- 检查内存泄漏

## 内存回收机制

### 自动回收
- 当cache中空slab数量超过阈值时自动回收
- 内存紧张时主动回收空slab

### 手动回收
```c
// 回收特定cache的空slab
int reclaimed = slab_reclaim_empty_slabs(cache, max_reclaim);
```

## 故障排除

### 常见问题

1. **编译错误**: 确保所有头文件正确包含
2. **运行时错误**: 检查内存初始化和锁的使用
3. **测试失败**: 查看详细的测试输出日志

### 调试技巧

1. 使用统计接口监控内存使用
2. 启用详细的调试输出
3. 检查碎片分析报告

## 文件结构

```
kernel/
├── slab.h          # Slab分配器头文件
├── slab.c          # Slab分配器实现
├── slab_test.h     # 测试头文件
└── slab_test.c     # 测试实现

run_slab_tests.sh   # 测试运行脚本
SLAB_TESTS.md       # 本文档
```

## 注意事项

1. 测试需要在xv6环境中运行
2. 某些测试可能需要较长时间完成
3. 建议在单CPU模式下运行测试以避免并发问题
4. 测试输出会保存在`test_output_*.log`文件中

## 贡献

如需添加新的测试或改进现有测试，请：
1. 在`slab_test.c`中添加新的测试case
2. 更新测试脚本和文档
3. 确保测试具有适当的错误检查和清理机制