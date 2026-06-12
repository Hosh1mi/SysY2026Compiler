#include<stdio.h>
int main(){
    int n = 5;
    int sum = 0;
    int i;
    i = 0;
    while(i < n){
        sum = sum + i;
        i = i + 1;
    }
    printf("%d\n", sum);
    return 0;
}