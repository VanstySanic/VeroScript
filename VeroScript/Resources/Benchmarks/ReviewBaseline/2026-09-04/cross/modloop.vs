int work() { int total = 0; for (int i = 0; i < 20000000; i++) { total = (total + (i % 97)) % 1000000007; } return total; } void main(){int checksum=0;for(int rep=0;rep<2;rep++){checksum=(checksum+work())%1000000007;}print(checksum);}

