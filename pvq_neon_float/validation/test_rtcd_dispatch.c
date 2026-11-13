#include "vq.h"
#include <stdio.h>
#include <string.h>
int main(void) {
  int i, a[8], b[8]; float x[8]={.1f,-.2f,.3f,-.4f,.5f,-.6f,.7f,-.8f};
  float ya,yb;
  for(i=0;i<3;i++)if(OP_PVQ_SEARCH_IMPL[i]!=op_pvq_search_c)return 1;
  for(i=3;i<5;i++)if(OP_PVQ_SEARCH_IMPL[i]!=op_pvq_search_neon)return 2;
  ya=op_pvq_search(x,a,7,8,3);yb=op_pvq_search_neon(x,b,7,8,3);
  if(ya!=yb||memcmp(a,b,sizeof(a)))return 3;
  puts("PASS: RTCD slots0..2=C,slots3..4=NEON; dispatched output equals direct NEON");
  return 0;
}
