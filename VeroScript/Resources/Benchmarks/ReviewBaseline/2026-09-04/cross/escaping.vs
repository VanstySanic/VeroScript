class Pair{public:int first=0;int second=0;int sum(){return first+second;}};
int work(int n){array<Pair>a=array<Pair>();for(int i=0;i<n;i++){Pair p=Pair();p.first=(i+17)&255;p.second=(i*7)&127;a.push(p);}int total=0;for(int i=n-1;i>=0;i--)total+=a[i].sum();return total;}
void main(){int checksum=0;for(int rep=0;rep<5;rep++){checksum=(checksum+work(20000))%1000000007;}print(checksum);}
