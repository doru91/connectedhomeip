/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "TestSupport.h"

#include <support/ScopedBuffer.h>
#include <support/TestUtils.h>

#include <nlunit-test.h>

namespace {

class TestCounterScopedBuffer : public chip::Platform::Impl::ScopedMemoryBufferBase<TestCounterScopedBuffer>
{
public:
    static int Counter() { return mAllocCount; }

    static void MemoryFree(void * p)
    {
        mAllocCount--;
        chip::Platform::MemoryFree(p);
    }
    static void * MemoryAlloc(size_t size)
    {
        mAllocCount++;
        return chip::Platform::MemoryAlloc(size);
    }
    static void * MemoryAlloc(size_t size, bool longTerm)
    {

        mAllocCount++;
        return chip::Platform::MemoryAlloc(size, longTerm);
    }
    static void * MemoryCalloc(size_t num, size_t size)
    {

        mAllocCount++;
        return chip::Platform::MemoryCalloc(num, size);
    }

private:
    static int mAllocCount;
};
int TestCounterScopedBuffer::mAllocCount = 0;

void TestAutoFree(nlTestSuite * inSuite, void * inContext)
{
    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);

    {
        TestCounterScopedBuffer buffer;

        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
        NL_TEST_ASSERT(inSuite, buffer.Alloc(128));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
    }
    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
}

void TestFreeDuringAllocs(nlTestSuite * inSuite, void * inContext)
{
    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);

    {
        TestCounterScopedBuffer buffer;

        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
        NL_TEST_ASSERT(inSuite, buffer.Alloc(128));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
        NL_TEST_ASSERT(inSuite, buffer.Alloc(64));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
        NL_TEST_ASSERT(inSuite, buffer.LongTermAlloc(256));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
        NL_TEST_ASSERT(inSuite, buffer.Calloc<uint64_t>(10));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
    }
    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
}

void TestRelease(nlTestSuite * inSuite, void * inContext)
{
    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
    void * ptr = nullptr;

    {
        TestCounterScopedBuffer buffer;

        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
        NL_TEST_ASSERT(inSuite, buffer.Alloc(128));
        NL_TEST_ASSERT(inSuite, buffer.Ptr() != nullptr);

        ptr = buffer.Release();
        NL_TEST_ASSERT(inSuite, ptr != nullptr);
        NL_TEST_ASSERT(inSuite, buffer.Ptr() == nullptr);
    }

    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);

    {
        TestCounterScopedBuffer buffer;
        NL_TEST_ASSERT(inSuite, buffer.Alloc(128));
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 2);
        TestCounterScopedBuffer::MemoryFree(ptr);
        NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 1);
    }

    NL_TEST_ASSERT(inSuite, TestCounterScopedBuffer::Counter() == 0);
}

} // namespace

#define NL_TEST_DEF_FN(fn) NL_TEST_DEF("Test " #fn, fn)
/**
 *   Test Suite. It lists all the test functions.
 */
static const nlTest sTests[] = {
    NL_TEST_DEF_FN(TestAutoFree),         //
    NL_TEST_DEF_FN(TestFreeDuringAllocs), //
    NL_TEST_DEF_FN(TestRelease),          //
    NL_TEST_SENTINEL()                    //
};

int TestScopedBuffer(void)
{
    nlTestSuite theSuite = { "CHIP ScopedBuffer tests", &sTests[0], nullptr, nullptr };

    // Run test suit againt one context.
    nlTestRunner(&theSuite, nullptr);
    return nlTestRunnerStats(&theSuite);
}

CHIP_REGISTER_TEST_SUITE(TestBufBound)