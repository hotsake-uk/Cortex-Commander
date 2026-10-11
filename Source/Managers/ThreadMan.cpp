#include "ThreadMan.h"

#include <algorithm>
#include <latch>
#include <thread>

#ifdef __APPLE__
#include <tbb/task_scheduler_observer.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

using namespace RTE;

#ifdef __APPLE__
namespace {
	/// Sets the QoS on TBB's worker threads as they join in, since TBB starts them itself (they run the std::execution::par_unseq loops: grid rebuilds, terrain).
	class TBBWorkerQoS : public tbb::task_scheduler_observer {
	public:
		TBBWorkerQoS() { observe(true); }
		void on_scheduler_entry(bool isWorker) override {
			// Workers come back in every time they pick up work, so set it once per thread.
			thread_local bool qosSet = false;
			if (isWorker && !qosSet) {
				ThreadMan::SetCurrentThreadQoS(false);
				qosSet = true;
			}
		}
	};
} // namespace
#endif

ThreadMan::ThreadMan() {
	Clear();
	Create();
}

ThreadMan::~ThreadMan() {
	Destroy();
}

void ThreadMan::Clear() {
	m_PriorityThreadPool.reset();
	m_BackgroundThreadPool.reset(std::thread::hardware_concurrency() / 2);
	ApplyQoS(m_PriorityThreadPool, false);
	ApplyQoS(m_BackgroundThreadPool, true);
}

int ThreadMan::Create() {
#ifdef __APPLE__
	// Never deleted, so it can't be torn down after TBB at exit.
	static TBBWorkerQoS* tbbWorkerQoS = new TBBWorkerQoS();
	(void)tbbWorkerQoS;
#endif
	return 0;
}

void ThreadMan::Destroy() {
	Clear();
}

int ThreadMan::GetPerformanceCoreCount() {
#ifdef __APPLE__
	int performanceCores = 0;
	size_t size = sizeof(performanceCores);
	if (sysctlbyname("hw.perflevel0.logicalcpu", &performanceCores, &size, nullptr, 0) == 0 && performanceCores > 0) {
		return performanceCores;
	}
#endif
	return static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
}

void ThreadMan::SetCurrentThreadQoS(bool background) {
#ifdef __APPLE__
	pthread_set_qos_class_self_np(background ? QOS_CLASS_USER_INITIATED : QOS_CLASS_USER_INTERACTIVE, 0);
#else
	(void)background;
#endif
}

void ThreadMan::ApplyQoS(BS::thread_pool& pool, bool background) {
	// Every task waits until all have started, so each thread takes exactly one.
	const auto threadCount = pool.get_thread_count();
	std::latch allStarted(threadCount);
	for (unsigned int i = 0; i < threadCount; ++i) {
		pool.push_task([&allStarted, background]() {
			SetCurrentThreadQoS(background);
			allStarted.arrive_and_wait();
		});
	}
	pool.wait_for_tasks();
}
