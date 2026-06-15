/*
    test_gc_map.c

    回归测试：woort_GCMap_erase 的 "move last bucket into deleted slot" 逻辑。

    历史缺陷：搬运时只复制了 m_key/m_val，却漏更新 m_next/m_prev，
    导致碰撞链断裂（丢元素）甚至成环（_woort_GCMap_find_bucket 死循环）。
    本测试用强制哈希碰撞构造可复现场景，并辅以大规模压力测试。
*/

#include "woort.h"
#include "woort_value.h"
#include "woort_gc_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 测试基础设施 ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-45s ", _test_name);            \
        fflush(stdout);

#define TEST_END()                                              \
        (void)printf("PASS\n");                                 \
        g_tests_passed++;                                       \
    } while(0)

#define TEST_ASSERT(cond)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            (void)printf("FAIL\n");                             \
            (void)printf("    assert failed: %s\n", #cond);     \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

/* ========== 辅助：收集 count 个在相同 entry 槽位碰撞的 int key ========== */

static int collect_colliding_keys(size_t mask, size_t count, woort_Int* out)
{
    const size_t nslots = mask + 1;
    woort_Int* grid = (woort_Int*)calloc(nslots * count, sizeof(woort_Int));
    size_t* cnt = (size_t*)calloc(nslots, sizeof(size_t));

    int ok = 0;
    for (woort_Int k = 1; k < 5000000; k++)
    {
        const woort_DynBox boxed = woort_DynBox_box_int(k);
        const size_t slot = woort_DynBox_hash(boxed) & mask;

        if (cnt[slot] < count)
        {
            grid[slot * count + cnt[slot]] = k;
            cnt[slot]++;
            if (cnt[slot] == count)
            {
                for (size_t i = 0; i < count; i++)
                    out[i] = grid[slot * count + i];
                ok = 1;
                break;
            }
        }
    }

    free(grid);
    free(cnt);
    return ok;
}

/* ========== 测试 1：确定性复现 erase 搬运破坏碰撞链 ========== */

static void test_gcmap_erase_keeps_collision_chain(void)
{
    TEST_BEGIN("gcmap_erase_movelast_keeps_chain");

    woort_GCMap* m = woort_GCMap_new();
    TEST_ASSERT(m != NULL);

    /* 容量 8 -> m_mask = 7；找 6 个落进同一槽位的 int key */
    woort_GCMap_reserve(m, 8);
    TEST_ASSERT(m->m_mask == 7);

    woort_Int keys[6];
    TEST_ASSERT(collect_colliding_keys(m->m_mask, 6, keys));

    /* 顺序插入：bucket 索引 0..5 全部位于同一碰撞链 */
    for (size_t i = 0; i < 6; i++)
    {
        woort_GCMap_set_or_insert(
            m, woort_DynBox_box_int(keys[i]), woort_DynBox_box_int(keys[i] * 7));
    }
    TEST_ASSERT(m->m_size == 6);

    /* 删除最早插入（非末尾 bucket）的 key，触发 move-last */
    TEST_ASSERT(woort_GCMap_erase(m, woort_DynBox_box_int(keys[0])));
    TEST_ASSERT(m->m_size == 5);

    /* 其余 5 个 key 必须仍然可查，且值正确 */
    for (size_t i = 1; i < 6; i++)
    {
        woort_DynBox val;
        const bool found =
            woort_GCMap_get(m, woort_DynBox_box_int(keys[i]), &val);
        TEST_ASSERT(found);
        TEST_ASSERT(woort_DynBox_equal(val, woort_DynBox_box_int(keys[i] * 7)));
    }

    /* 被删除的 key 必须已不存在 */
    {
        woort_DynBox val;
        TEST_ASSERT(!woort_GCMap_get(m, woort_DynBox_box_int(keys[0]), &val));
    }

    TEST_END();
}

/* ========== 测试 2：连续删除多个非末尾 bucket ========== */

static void test_gcmap_erase_multiple_in_chain(void)
{
    TEST_BEGIN("gcmap_erase_multiple_in_same_chain");

    woort_GCMap* m = woort_GCMap_new();
    TEST_ASSERT(m != NULL);

    woort_GCMap_reserve(m, 8);

    woort_Int keys[6];
    TEST_ASSERT(collect_colliding_keys(m->m_mask, 6, keys));

    for (size_t i = 0; i < 6; i++)
    {
        woort_GCMap_set_or_insert(
            m, woort_DynBox_box_int(keys[i]), woort_DynBox_box_int(keys[i]));
    }

    /* 删除 keys[0] 和 keys[2]（均为非末尾 bucket，触发两次搬运） */
    TEST_ASSERT(woort_GCMap_erase(m, woort_DynBox_box_int(keys[0])));
    TEST_ASSERT(woort_GCMap_erase(m, woort_DynBox_box_int(keys[2])));
    TEST_ASSERT(m->m_size == 4);

    /* 剩余 keys[1],[3],[4],[5] 必须全部命中 */
    const size_t remain_idx[] = { 1, 3, 4, 5 };
    for (size_t n = 0; n < sizeof(remain_idx) / sizeof(remain_idx[0]); n++)
    {
        woort_DynBox val;
        const bool found =
            woort_GCMap_get(m, woort_DynBox_box_int(keys[remain_idx[n]]), &val);
        TEST_ASSERT(found);
        TEST_ASSERT(
            woort_DynBox_equal(val, woort_DynBox_box_int(keys[remain_idx[n]])));
    }

    {
        woort_DynBox val;
        TEST_ASSERT(!woort_GCMap_get(m, woort_DynBox_box_int(keys[0]), &val));
        TEST_ASSERT(!woort_GCMap_get(m, woort_DynBox_box_int(keys[2]), &val));
    }

    TEST_END();
}

/* ========== 测试 3：大规模压力测试（避免扩容自愈） ========== */

static void test_gcmap_erase_stress(void)
{
    TEST_BEGIN("gcmap_erase_stress_no_lost_no_loop");

    woort_GCMap* m = woort_GCMap_new();
    TEST_ASSERT(m != NULL);

    const woort_Int N = 1500;

    /* 预留足够容量：插入阶段不会触发 reserve/rehash，
       从而保证 erase 阶段产生的链表损坏不会被 rehash 自愈。 */
    woort_GCMap_reserve(m, (size_t)N);
    const size_t reserved_mask = m->m_mask;

    for (woort_Int k = 1; k <= N; k++)
    {
        woort_GCMap_set_or_insert(
            m, woort_DynBox_box_int(k), woort_DynBox_box_int(k));
    }

    /* m_mask 在插入过程中不得改变（说明没有发生 rehash） */
    TEST_ASSERT(m->m_mask == reserved_mask);

    /* 删除所有奇数 key：大量非末尾搬运 */
    woort_Int erased_count = 0;
    for (woort_Int k = 1; k <= N; k += 2)
    {
        TEST_ASSERT(woort_GCMap_erase(m, woort_DynBox_box_int(k)));
        erased_count++;
    }
    TEST_ASSERT(m->m_size == (size_t)(N - erased_count));

    /* 所有偶数 key 必须命中，所有奇数 key 必须已消失 */
    for (woort_Int k = 1; k <= N; k++)
    {
        woort_DynBox val;
        const bool found = woort_GCMap_get(m, woort_DynBox_box_int(k), &val);
        if ((k & 1) == 1)
        {
            TEST_ASSERT(!found);
        }
        else
        {
            TEST_ASSERT(found);
            TEST_ASSERT(woort_DynBox_equal(val, woort_DynBox_box_int(k)));
        }
    }

    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        (void)printf("FATAL: VMRuntime create failed\n");
        woort_shutdown(NULL, NULL);
        return 1;
    }
    (void)woort_VMRuntime_swap(vm);

    (void)printf("\n=== GCMap erase regression tests ===\n\n");

    test_gcmap_erase_keeps_collision_chain();
    test_gcmap_erase_multiple_in_chain();
    test_gcmap_erase_stress();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);
    woort_shutdown(NULL, NULL);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
