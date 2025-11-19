#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/vm.h"
#include "user/user.h"

static void err(const char *why) {
  printf("khugepagedtest: %s failed, pid=%d\n", why, getpid());
  exit(1);
}

int
main(int argc, char *argv[])
{
  printf("khugepagedtest: start\n");

  // 1) 对齐到 2MB 边界，避免 uvmalloc 直接分配超页
  uint64 brk = (uint64) sbrk(0);
  uint64 aligned = SUPERPGROUNDUP(brk);
  if (aligned > brk) {
    if (sbrk(aligned - brk) == (void*)SBRK_ERROR)
      err("sbrk align failed");
  }

  char *base = (char*) sbrk(0);
  if (base == (void*)SBRK_ERROR) err("sbrk(0) failed");

  // 2) 逐页扩展并写入，保证都是 4KB 页（避免一次性 2MB 导致直接超页）
  for (int i = 0; i < 512; i++) {
    char *p = (char*) sbrk(PGSIZE);
    if (p == (void*)SBRK_ERROR)
      err("sbrk page-wise failed");
    // 触碰该页，确保页表项建立
    *(volatile char*)(base + i * PGSIZE) = (char)i;
  }

  // 3) 合并前，采样两个相邻页的 PTE 值（通常应不同）
  pte_t before1 = (pte_t) pgpte((void*) base);
  pte_t before2 = (pte_t) pgpte((void*) (base + PGSIZE));
  printf("before collapse: PTE[base]=0x%lx PTE[base+PGSIZE]=0x%lx\n", before1, before2);

  // 4) 等待 khugepaged 扫描与合并（tick ~0.1s），这里睡 3 秒
  sleep(30);

  // 5) 合并后再次检查，若变为同一 PTE 值，表示 L1 超页映射生效
  pte_t after1 = (pte_t) pgpte((void*) base);
  pte_t after2 = (pte_t) pgpte((void*) (base + PGSIZE));
  printf("after collapse:  PTE[base]=0x%lx PTE[base+PGSIZE]=0x%lx\n", after1, after2);

  if (after1 != 0 && after1 == after2) {
    printf("khugepagedtest: collapse success (hugepage mapped)\n");
  } else {
    printf("khugepagedtest: collapse not observed (try longer sleep or more activity)\n");
  }

  // 6) 可选：释放这 2MB 区域，避免影响后续测试
  if (sbrk(-SUPERPGSIZE) == (void*)SBRK_ERROR) {
    err("sbrk free failed");
  }

  printf("khugepagedtest: done\n");
  exit(0);
}