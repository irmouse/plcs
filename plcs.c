#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "thread.h"
#include "thread-sync.h"

#define MAXN 10000
int T, N, M;
char A[MAXN + 1], B[MAXN + 1];
int dp[MAXN][MAXN];
int result;

#define DP(x, y) (((x) >= 0 && (y) >= 0) ? dp[x][y] : 0)
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MAX3(x, y, z) MAX(MAX(x, y), z)
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

mutex_t mutex;
cond_t cv;

typedef struct job
{
  void (*run)(void *arg);
  void *arg;
} job;

int all_done;
struct
{
  int job_todo;
  int job_done;
  // 起始点坐标：x,y
  int x;
  int y;
} job_list;

typedef struct dp_arg
{
  int i;
  int j;
} dp_arg;

void run_dp(void *arg)
{
  dp_arg *args = (dp_arg *)arg;
  int i = args->i;
  int j = args->j;
  printf("i: %d, j: %d, ", i, j);
  int skip_a = DP(i - 1, j);
  int skip_b = DP(i, j - 1);
  int take_both = DP(i - 1, j - 1) + (A[i] == B[j]);
  printf("skip_a: %d, skip_b: %d, take_both: %d\n",skip_a, skip_b, take_both);
  dp[i][j] = MAX3(skip_a, skip_b, take_both);
  mutex_lock(&mutex);
  job_list.job_done--;
  if (job_list.job_done == 0)
  {
    // 如果是发现所有job都做完了，那么就需要通知
    // id=1的线程了
    cond_broadcast(&cv);
  }
  mutex_unlock(&mutex);
}

// REQUIRE: with mutex
job *get_job()
{
  if (job_list.job_todo == 0)
    return NULL;
  job *job_instance = (job *)malloc(sizeof(job));
  dp_arg *job_arg = (dp_arg *)malloc(sizeof(dp_arg));
  job_instance->arg = job_arg;
  job_instance->run = &run_dp;

  job_arg->i = job_list.x;
  job_arg->j = job_list.y;
  job_list.x--;
  job_list.y++;
  job_list.job_todo--;

  return job_instance;
}

void release_job(job **j)
{
  assert(j != NULL);
  assert((*j)->arg != NULL);
  free((*j)->arg);
  if (*j != NULL)
  {
    free(*j);
    *j = NULL;
  }
}

void OneThread()
{
  for (int i = 0; i < N; i++)
  {
    for (int j = 0; j < M; j++)
    {
      // Always try to make DP code more readable
      int skip_a = DP(i - 1, j);
      int skip_b = DP(i, j - 1);
      int take_both = DP(i - 1, j - 1) + (A[i] == B[j]);
      dp[i][j] = MAX3(skip_a, skip_b, take_both);
    }
  }

  result = dp[N - 1][M - 1];
}

void Tworker(int id)
{
  if (id == 1)
  {
    if (T == 1)
    {
      OneThread();
      return;
    }
    // 串行实现，只有一个线程执行该部分代码
    // 当任务都完成后执行下一部分的任务
    int start[2], end[2];
    start[0] = start[1] = end[0] = end[1] = 0;
    for (int round = 0; round < M + N - 1; round++)
    {
      // 1. 计算出本轮能够计算的单元格
      int point_num = (start[0] - end[0]) + 1;  // 注意：一定是个正方形
      printf("start: %d, %d; end: %d, %d; point_num: %d\n", start[0], start[1], end[0], end[1], point_num);
      assert(point_num <= MIN(M, N));
      // 2. 将任务分配给线程执行
      mutex_lock(&mutex);
      assert(job_list.job_done == 0);
      job_list.job_todo = point_num;
      job_list.job_done = point_num;
      job_list.x = start[0];
      job_list.y = start[1];
      cond_broadcast(&cv); // 通知其他线程执行任务
      // 3. 等待线程执行完毕
      while (!(job_list.job_todo == 0))
      {
        cond_wait(&cv, &mutex);
      }
      assert(job_list.job_todo == 0);
      mutex_unlock(&mutex);
      // 4. 更新起止点位置
      if (start[0] < N - 1)
        start[0]++;
      else
        start[1]++;
      if (end[1] < M - 1)
        end[1]++;
      else
        end[0]++;
    }
    mutex_lock(&mutex);
    all_done = 1;
    cond_broadcast(&cv);
    mutex_unlock(&mutex);
    result = dp[N - 1][M - 1];
    return;
  }
  // 对于一个Worker，它只会执行job
  while (1)
  {
    struct job *work;
    mutex_lock(&mutex);
    while (!((work = get_job()) || all_done))
    { // 如果没有获得到job，就会进行等待
      cond_wait(&cv, &mutex);
    }
    mutex_unlock(&mutex);
    if (!work)
      break; // 因为是通过all_done条件退出的
    // 得到了job，就会在没有锁的时候去执行它
    work->run(work->arg);
    release_job(&work); // 注意回收 work 分配的资源
    assert(work == NULL);
  }
}

int main(int argc, char *argv[])
{
  // No need to change
  assert(scanf("%s%s", A, B) == 2);
  N = strlen(A);
  M = strlen(B);
  T = !argv[1] ? 1 : atoi(argv[1]);
  assert(T >= 1);
  // Add preprocessing code here
  all_done = 0;
  job_list.job_todo = 0;
  job_list.job_done = 0;
  printf("str1: %s; str2: %s\n", A, B);
  for (int i = 0; i < T; i++)
  {
    create(Tworker);
  }
  join(); // Wait for all workers

  printf("%d\n", result);
}
