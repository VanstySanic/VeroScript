#define main generated_script_main
#include AOT_FILE
#undef main
#include <chrono>
#include <iomanip>
volatile int aot_sink=0;
int main(int argc,char**argv){if(argc<2)return 2;int n=std::atoi(argv[1]);vs_ensure_globals();int expected=AOT_FUNCTION(n);int batch=1;using C=std::chrono::steady_clock;for(;;){auto t=C::now();for(int i=0;i<batch;i++)aot_sink=AOT_FUNCTION(n);if(std::chrono::duration<double,std::milli>(C::now()-t).count()>=30||batch>=10000)break;batch*=2;}std::cout<<std::setprecision(10);for(int s=0;s<7;s++){auto t=C::now();for(int i=0;i<batch;i++){int v=AOT_FUNCTION(n);if(v!=expected)return 3;aot_sink=v;}double dt=std::chrono::duration<double,std::milli>(C::now()-t).count();std::cout<<s<<','<<dt/batch<<','<<batch<<','<<expected<<'\n';}}
