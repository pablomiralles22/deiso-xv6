#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/pstat.h"

#define N_THREADS 5
#define X_SPIN 1e5
#define Y_SPIN 1e5
#define ITER_TOL 5

struct pstat ps;
int pid[N_THREADS], ind[N_THREADS];
long long aux;

long long spin() {
  unsigned x = 0;
  unsigned y = 0;

  while (x < X_SPIN) {
    y = 0;
    while (y < Y_SPIN) y++, aux++;
    x++;
  }
  return aux;
}

int main(int argc, char *argv[])
{
  int pid_aux;
  settickets((2*N_THREADS+1)*10);
  for(int i=0; i<N_THREADS; ++i)
    if ((pid_aux = fork()) == 0) {
      settickets((i+1) * 10);
      spin();
      exit(0); 
    } else pid[i] = pid_aux;

  getpinfo(&ps);
  for (int i = 0; i < NPROC; i++)
    if(ps.inuse[i])
      for(int j = 0; j < N_THREADS; ++j)
        if(ps.pid[i] == pid[j])
          ind[j] = i;

  int last_sum_ticks = 0;
  int cnt_iter_no_ticks = 0;
  for(;;) {
    getpinfo(&ps);

    int sum_ticks = 0;

    for (int j = 0; j < N_THREADS; ++j)
      if(ps.inuse[ind[j]] && ps.pid[ind[j]] == pid[j])
          sum_ticks += ps.ticks[ind[j]];

    if(sum_ticks <= last_sum_ticks) cnt_iter_no_ticks++;
    else cnt_iter_no_ticks = 0;

    if(cnt_iter_no_ticks >= ITER_TOL) break;
    last_sum_ticks = sum_ticks;

    for(int j=0; j<N_THREADS; ++j)
      printf("%d ", ps.ticks[ind[j]]);
    printf("\n");
    sleep(2);
  }

  exit(0);
}
