/*
 * CFG-12: C++ consumer (spec section 15). The public headers must compile as C++11 with strict
 * warnings, the layout macros must work in C++ constant expressions, and the C API must link and
 * behave as specified when called from C++.
 */
#include <cstdint>
#include <cstring>

#include "bloc/bloc.h"
#include "unity.h"

namespace
{

const bloc_count_t count = 4u;
const bloc_size_t element_size = 64u;

BLOC_POOL_STORAGE(storage, 4u, 64u);
static_assert(sizeof(storage) == BLOC_POOL_SIZE(4u, 64u), "BLOC_POOL_SIZE is constant");
BLOC_STATIC_ASSERT(BLOC_ALIGNOF(struct bloc_handle) <= BLOC_STORAGE_ALIGNMENT,
                   "BLOC_STORAGE_ALIGNMENT covers the handle alignment");

} // namespace

void setUp(void) {}

void tearDown(void) {}

static void test_CFG_12_cpp_consumer(void)
{
    bloc_pool_t pool;
    std::memset(&pool, 0, sizeof(pool));

    TEST_ASSERT_EQUAL_UINT(0u, reinterpret_cast<std::uintptr_t>(storage) % BLOC_STORAGE_ALIGNMENT);
    TEST_ASSERT_EQUAL_INT(BLOC_OK,
                          bloc_pool_init(&pool, storage, sizeof(storage), count, element_size));
    TEST_ASSERT_EQUAL_UINT(count, bloc_pool_free_count(&pool));

    bloc_handle_t b = bloc_alloc(&pool, 8u);
    TEST_ASSERT_NOT_NULL(b);
    const std::uint8_t payload[5] = {1u, 2u, 3u, 4u, 5u};
    TEST_ASSERT_EQUAL_INT(BLOC_OK, bloc_append_data(b, payload, sizeof(payload)));

    bloc_const_handle_t cb = b;
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), bloc_len(cb));
    TEST_ASSERT_EQUAL_UINT(BLOC_ALIGN_UP(8u, BLOC_PAYLOAD_ALIGNMENT), bloc_headroom(cb));
    TEST_ASSERT_EQUAL_UINT(element_size, bloc_headroom(cb) + bloc_len(cb) + bloc_tailroom(cb));
    TEST_ASSERT_EQUAL_MEMORY(payload, bloc_data(cb), sizeof(payload));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(count, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, bloc_pool_deinit(&pool));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CFG_12_cpp_consumer);
    return UNITY_END();
}
