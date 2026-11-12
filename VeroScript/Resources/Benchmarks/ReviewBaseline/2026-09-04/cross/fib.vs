int fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}
int work(int n){return fib(n);}
void main(){int checksum=0;for(int rep=0;rep<5;rep++){checksum=(checksum+work(24))%1000000007;}print(checksum);}
