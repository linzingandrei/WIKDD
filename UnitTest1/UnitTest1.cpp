#include "pch.h"
#include "CppUnitTest.h"
#include "../ConsoleApplication1/ConsoleApplication1.cpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTest1
{
	TEST_CLASS(UnitTest1)
	{
	public:
		
		TEST_METHOD(TestMethod1)
		{
            MY_THREAD_POOL tp = { 0 };
            MY_CONTEXT ctx = { 0 };
            NTSTATUS status = STATUS_UNSUCCESSFUL;

            status = TpInit(&tp, 5);
            if (!NT_SUCCESS(status))
            {
                goto CleanUp;
            }

            InitializeSRWLock(&ctx.ContextLock);
            ctx.Number = 0;

            for (int i = 0; i < 100000; ++i)
            {

                status = TpEnqueueWorkItem(&tp, TestThreadPoolRoutine, &ctx);
                if (!NT_SUCCESS(status))
                {
                    goto CleanUp;
                }
            }

            TpUninit(&tp);

            assert(ctx.Number == 100000000);

        CleanUp:
            _CrtDumpMemoryLeaks();
		}
	};
}
