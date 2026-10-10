#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace perf_v1 {
void Memory(const char *Phase);
struct Timer {
 const char *m_Phase; std::chrono::steady_clock::time_point m_Start=std::chrono::steady_clock::now();
 explicit Timer(const char *Phase):m_Phase(Phase){}
 ~Timer(){std::printf("PERF phase=%s ms=%.6f\n",m_Phase,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-m_Start).count()); std::fflush(stdout);}
};
inline void Check(bool Condition,const char *Message) {if(!Condition){ std::printf("PERF_FAIL %s\n",Message); std::fflush(stdout); std::abort(); }}
}
