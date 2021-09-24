#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/pstat.h"

#define N_THREADS 3
#define X_SPIN 1e5
#define Y_SPIN 1e4

struct pstat ps;
int pid[N_THREADS];

void spin() {
  unsigned x = 0;
  unsigned y = 0;

  while (x < 100000) {
    y = 0;
    while (y < 10000) y++;
    x++;
  }
}

int main(int argc, char *argv[])
{
  int pid_aux;
  for(int i=0; i<N_THREADS; ++i)
    if ((pid_aux = fork()) == 0) {
      spin();
      exit(0); 
    } else pid[i] = pid_aux;

  for(int i=0; i<N_THREADS; ++i)
    printf("%d ", pid[i]);
  printf("\n\n");

  getpinfo(&ps);
  for (int j = 0; j < NPROC; j++)
    printf("%d %d %d %d\n", ps.pid[j], ps.inuse[j], ps.tickets[j], ps.ticks[j]);

  exit(0);
}
