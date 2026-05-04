#include <stdio.h>

 int main() {                                                                                                                                                                        
      double result = 1.0;
      double base = 3.0;
      volatile double sink;                                                                                                                                                             
   
      while (1) {                                                                                                                                                                       
          result = result * base;                                                                                                                                                     
          if (result > 1e15) result = 1.0;
          sink = result;                                                                                                                                                                
      }
                                                                                                                                                                                        
      return 0;                                                                                                                                                                       
  }

