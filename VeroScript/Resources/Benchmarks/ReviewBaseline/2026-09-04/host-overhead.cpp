#include "VeroScriptCompiler.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace VeroScript;using C=std::chrono::steady_clock;
int main(){const char*src="int add(int a,int b){return a+b;}void main(){}";VeroScriptModuleLoader loader("perf.vs");loader.SetSourceOverlay("perf.vs",src);auto p=loader.LoadSourceModule(src,"perf.vs");VeroScriptCompiler cc;auto m=cc.Compile(*p);if(!m)return 2;std::uint16_t idx=0;while(idx<m->Functions.size()&&m->Functions[idx].Name.find("add#")==std::string::npos)idx++;if(idx>=m->Functions.size())return 3;std::cout<<std::setprecision(9);
for(auto b:{EExecutionBackend::Vm,EExecutionBackend::Mixed,EExecutionBackend::Jit}){std::ostringstream out;auto vm=std::make_unique<VeroScriptVm>(out);vm->SetExecutionBackend(b);std::vector<FValue> args{int32_t(17),int32_t(31)};for(int s=-1;s<7;s++){auto t=C::now();for(int i=0;i<1000;i++){FValue result;if(!vm->ExecuteFunction(*m,idx,args,nullptr,&result)||std::get<int32_t>(result)!=48)return 4;}std::cout<<ExecutionBackendName(b)<<','<<s<<','<<std::chrono::duration<double,std::micro>(C::now()-t).count()/1000<<'\n';}}}
