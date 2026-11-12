int work(int n){array<int>a=array<int>();for(int i=0;i<=n;i++)a.push(0);int count=0;for(int p=2;p<=n;p++){if(a[p]==0){count++;if(p<=n/p){for(int j=p*p;j<=n;j+=p)a[j]=1;}}}return count;}
void main(){int checksum=0;for(int rep=0;rep<5;rep++){checksum=(checksum+work(100000))%1000000007;}print(checksum);}
