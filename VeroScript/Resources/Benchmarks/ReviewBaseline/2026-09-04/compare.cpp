#include <array>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
using C=std::chrono::steady_clock;
using Fn=int(*)(int,int);
volatile int sink=0;
__declspec(noinline) int modloop(int n,int seed){int total=seed;for(int i=0;i<n;i++)total=(total+i%97)%1000000007;return total;}
__declspec(noinline) int indexed(int n,int seed){std::array<double,7> a={.125,-.75,1.5,.0625,-1.25,.875,.375};int state=seed;double v=.25;for(int i=0;i<n;i++){state=(state*25173+13849)%65521;v=v*.99991+a[state%7]*.00009;}return int((v+4)*1000000)+state;}
__declspec(noinline) int indirect(int n,int seed){std::vector<int> a,indices;for(int i=0;i<257;i++){a.push_back((i*17+11)%101);indices.push_back((i*37+19)%257);}int state=seed,total=0;for(int i=0;i<n;i++){state=(state*25173+13849)%65521;int pos=(state+((i%257)*7))%257,idx=indices[pos],delta=((state/7)%13)+1;a[idx]=(a[idx]+delta)%1000003;total=(total+a[idx]+(pos&3))%1000000007;}return (total+state+a[indices[state%257]])%1000000007;}
struct Pair{int first=0,second=0;int sum(){return first+second;}};
__declspec(noinline) int objects(int n,int seed){int state=seed,total=0;for(int i=0;i<n;i++){state=(state*25173+13849)%65521;Pair p;p.first=state%127;p.second=(state/7)%127;total=(total+p.sum())%1000000007;}return total+state;}
struct Op{virtual int step(int v,int salt)=0;virtual ~Op()=default;};struct Add:Op{int step(int v,int salt)override{return(v+1+salt%3)%1000000007;}};struct Xor:Op{int step(int v,int salt)override{return((v^(salt&7))+7)%1000000007;}};
__declspec(noinline) int virtuals(int n,int seed){Add a;Xor x;std::array<Op*,2> ops={&a,&x};std::array<int,4> order={1,0,1,0};int state=seed,total=0;for(int i=0;i<n;i++){state=(state*75+74)%65521;int idx=order[(state+total)&3];total=ops[idx]->step(total,state);}return total+state;}
__declspec(noinline) int maps(int n,int seed){std::unordered_map<int,double>a;a.reserve(1021);for(int i=0;i<1021;i++)a[i]=(i%31)*.25+1;int state=seed;double total=0;for(int i=0;i<n;i++){state=(state*75+74)%65521;int key=(state+i*13)%1021;double v=a[key]*1.000001+(state%7)*.25;if(v>10000)v*=.5;a[key]=v;total+=v;}return int(total)+state;}
__declspec(noinline) int strings(int n,int seed){std::array<std::string,7>l={"alpha-000","beta-111","gamma-222","delta-333","epsilon-444","zeta-555","eta-666"},r={"alpha-000","beta-X11","gamma-222","delta-X33","epsilon-444","zeta-X55","eta-666"};int state=seed,total=0;for(int i=0;i<n;i++){state=(state*75+74)%65521;int a=state%7,b=(state+(i&3))%7;if(l[a]==r[b])total++;}return total+state;}
__declspec(noinline) int fibimpl(int n){return n<2?n:fibimpl(n-1)+fibimpl(n-2);}
__declspec(noinline) int fib(int n,int seed){return fibimpl(n)+seed;}
__declspec(noinline) int sieve(int n,int seed){std::vector<int>a(n+1,0);int count=seed;for(int p=2;p<=n;p++){if(a[p]==0){count++;if(p<=n/p)for(int j=p*p;j<=n;j+=p)a[j]=1;}}return count;}
__declspec(noinline) int matrix(int n,int seed){std::vector<int>a,b,c(n*n,0);for(int i=0;i<n*n;i++){a.push_back((i*17+seed)%97);b.push_back((i*31+7)%89);}for(int i=0;i<n;i++)for(int k=0;k<n;k++)for(int j=0;j<n;j++)c[i*n+j]=(c[i*n+j]+a[i*n+k]*b[k*n+j])%1000000007;int sum=0;for(int v:c)sum=(sum+v)%1000000007;return sum;}
__declspec(noinline) int escaping(int n,int seed){std::vector<std::shared_ptr<Pair>>a;for(int i=0;i<n;i++){auto p=std::make_shared<Pair>();p->first=(i+seed)&255;p->second=(i*7)&127;a.push_back(p);}int total=0;for(int i=n-1;i>=0;i--)total+=a[i]->sum();return total;}
int main(int argc,char**argv){if(argc<4)return 2;std::string name=argv[1];int n=std::atoi(argv[2]),seed=std::atoi(argv[3]);std::unordered_map<std::string,Fn>f={{"modloop",modloop},{"indexed",indexed},{"indirect",indirect},{"objects",objects},{"virtuals",virtuals},{"maps",maps},{"strings",strings},{"fib",fib},{"sieve",sieve},{"matrix",matrix},{"escaping",escaping}};Fn fn=f.at(name);int expected=fn(n,seed);if(argc>4&&std::string(argv[4])=="once"){std::cout<<expected;return 0;}int batch=1;for(;;){auto t=C::now();for(int i=0;i<batch;i++)sink=fn(n,seed);double dt=std::chrono::duration<double,std::milli>(C::now()-t).count();if(dt>=30||batch>=10000)break;batch*=2;}std::cout<<std::setprecision(10);for(int s=0;s<7;s++){auto t=C::now();for(int i=0;i<batch;i++){int v=fn(n,seed);if(v!=expected)return 3;sink=v;}double dt=std::chrono::duration<double,std::milli>(C::now()-t).count();std::cout<<name<<",cpp,"<<s<<','<<dt/batch<<','<<batch<<','<<expected<<'\n';}}
