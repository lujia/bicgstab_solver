#include "stdio.h"
#include "stdlib.h"
#include "math.h"
#include "omp.h"

#include "create_csr.h"
#include "algorithm_par.h"

#define ARG_NX 1
#define ARG_NY 2
#define ARG_NZ 3
#define ARG_TOL 4
#define ARG_MAXIT 5
#define ARG_NT 6
#define ARG_QA 7
#define N_ARGS 7

#define ARG_ERROR 1

#define TEST_AXPBY 0
#define TEST_SPMV 1
#define TEST_DOT 2

const char *format = "%s\ttime=%6.3fs GFLOPS=%6.2f Speedup=%6.2fX NTR=%d\n";

void usage() {
  fprintf(stderr, "Usage: main [NX] [NY] [NZ] [TOL] [MAXIT] [THREADS] [QA]\n");
  exit(ARG_ERROR);
}

double bin = 0;

double test_op(int op_num, int n_tests, Vector XX, Vector YY, CSRMatrix A) {
  double op_time = 0.0;
  double t;
  double a = 1.1;
  double b = 0.99;
  double sum = 0;
  for (int i = 0; i < n_tests; i++) {
    for (int j = 0; j < n_tests; j++) {
      t = omp_get_wtime();
      if (op_num == TEST_AXPBY) {
        axpby_par(XX, YY, a, b);
      } else if (op_num == TEST_SPMV) {
        SpMV_par(A, XX, YY);
      } else if (op_num == TEST_DOT) {
        sum += dot_par(XX, YY);
      }
      op_time += omp_get_wtime() - t;  
    }
  }
  bin = sum;
  return op_time;
}
void test_basic_ops(int max_values[], long long N) {
  Vector XX = make_vector(N);
  Vector YY = make_vector(N);
  for (int i = 0; i < N; i++) {
    XX.data[i] = sin(i + 1);
    YY.data[i] = cos(i + 1);
  }
  CSRMatrix A = csr_matrix(max_values);
  int non_zero = A.IA[A.N];
  long long n_tests_dot = 300;
  long long n_tests_axpby = 200;
  long long n_tests_spmv = 20;

  int ntr = 1;

  // Testing basic operations 
  const double dot_gflop = n_tests_dot * n_tests_dot * (2*N-1) * 1E-9; // a[i]*b[i]
  const double axpby_gflop = n_tests_axpby * n_tests_axpby * 3*N * 1E-9; // a * x[i] + b * y[i] 
  const double spmv_gflop = n_tests_spmv * n_tests_spmv * (2*non_zero-A.N) * 1E-9; // a[i,j] * b[j]
  printf("DOT_GFLOP=%13.3f\n", dot_gflop);
  printf("AXPBY_GFLOP=%13.3f\n", axpby_gflop);
  printf("SPMV_GFLOP=%13.3f\n", spmv_gflop);
  printf("testing sequential ops:\n"); 
  omp_set_num_threads(ntr);
  double dot_time = test_op(TEST_DOT, n_tests_dot, XX, YY, A);
  double axpby_time = test_op(TEST_AXPBY, n_tests_axpby, XX, YY, A);
  double spmv_time = test_op(TEST_SPMV, n_tests_spmv, XX, YY, A);
  printf("Sequential ops timing: \n");
  printf(format, "dot", dot_time,  dot_gflop / dot_time, 1.0, ntr);
  printf(format, "axpby", axpby_time,  axpby_gflop / axpby_time, 1.0, ntr);
  printf(format, "spmv", spmv_time,  spmv_gflop / spmv_time, 1.0, ntr);
  
  //parallel mode  
  const int NTR = omp_get_num_procs();
  for(ntr = 2; ntr <= NTR; ntr *= 2) {
    printf("testing parallel ops for ntr=%d:\n", ntr);
    omp_set_num_threads(ntr);
    double dot_time_par = test_op(TEST_DOT, n_tests_dot, XX, YY, A);
    double axpby_time_par = test_op(TEST_AXPBY, n_tests_axpby, XX, YY, A);
    double spmv_time_par = test_op(TEST_SPMV, n_tests_spmv, XX, YY, A);
    printf(format, "dot", dot_time_par, dot_gflop/dot_time_par, dot_time/dot_time_par, ntr);
    printf(format, "axpby", axpby_time_par, axpby_gflop/axpby_time_par, axpby_time/axpby_time_par, ntr);
    printf(format, "spmv", spmv_time_par, spmv_gflop/spmv_time_par, spmv_time/spmv_time_par, ntr);
  }

  csr_free(&A);
  free_vector(&XX);
  free_vector(&YY);
}


double solver_results[2];
void test_solver(CSRMatrix A, Vector BB, Vector XX, double tol, int maxit, int n_tests) {
  const int non_zero = A.IA[A.N];
  const double AXPBY_GFLOP = 3*A.N * 1E-9;
  const double SPMV_GFLOP = (2*non_zero-A.N) * 1E-9;
  const double DOT_GFLOP = (2*A.N-1) * 1E-9;
  double total_gflop = 0.0;
  double total_time = 0.0;
  for (int i = 0; i < n_tests; i++) {
    double t = omp_get_wtime();
    int i = bicgstab_solve_par(A, BB, XX, tol, maxit, 0);
    t = omp_get_wtime() - t;
    total_time += t;
    double gflop = 1*DOT_GFLOP + 4*(AXPBY_GFLOP + SPMV_GFLOP + DOT_GFLOP) + i * (6*AXPBY_GFLOP + 4*SPMV_GFLOP + 5*DOT_GFLOP);
    total_gflop += gflop;
  }
  solver_results[0] = total_gflop;
  solver_results[1] = total_time;
}

void test_solver_with_threads(int max_values[], int N, double tol, int maxit) {
  printf("\ntesting sequential solver:\n");
  int ntr = 1;
  omp_set_num_threads(ntr);
  int n_tests = 10;
  CSRMatrix A = csr_matrix(max_values);
  Vector XX = make_vector(N);
  Vector BB = make_vector(N);
  for (int i = 0; i < N; i++) {
    BB.data[i] = sin(i + 1);
  }
  test_solver(A, BB, XX, tol, maxit, n_tests);
  double seq_solver_gflop = solver_results[0];
  double seq_solver_time = solver_results[1];
  printf("Sequential solver timing: \n");
  printf(format, "solver", seq_solver_time,  seq_solver_gflop / seq_solver_time, 1.0, ntr);
  
  // parallel mode
  const int NTR = omp_get_num_procs();
  for(ntr = 2; ntr <= NTR; ntr *= 2) {
    printf("testing parallel solver for ntr=%d:\n", ntr);
    omp_set_num_threads(ntr);
    test_solver(A, BB, XX, tol, maxit, n_tests);
    double par_solver_gflop = solver_results[0];
    double par_solver_time = solver_results[1];

    printf(format, "solver", par_solver_time, par_solver_gflop/par_solver_time, seq_solver_time/par_solver_time, ntr);
  }
}

int main(int argc, char **argv) {
  if (argc < N_ARGS) {
    usage();
  }

  int nx = atoi(argv[ARG_NX]);
  int ny = atoi(argv[ARG_NY]);
  int nz = atoi(argv[ARG_NZ]);

  double tol = strtod(argv[ARG_TOL], NULL);

  int maxit = atoi(argv[ARG_MAXIT]);
  int nt = atoi(argv[ARG_NT]);
  int qa = atoi(argv[ARG_QA]);


  int max_values[3];
  max_values[0] = nx;
  max_values[1] = ny;
  max_values[2] = nz;
  int N = nx*ny*nz;

  printf("Testing BiCGSTAB solver for a 3D grid domain\n");
  printf("nx=%d ny=%d nz=%d tol=%lf maxit=%d nt=%d qa=%d\n\n", nx, ny, nz, tol, maxit, nt, qa);
  printf("N   = %d (Nx=%d, Ny=%d, Nz=%d\n", N, nx, ny, nz);
  printf("Aij = sin(i+j+1), i != j\n");
  printf("Aii = 1.1*sum(fabs(Aij))\n");
  printf("Bi  = sin(i+1)\n");
  printf("tol = %.10e\n\n", tol);
 

  if (qa) {
    test_basic_ops(max_values, N);
  }
  test_solver_with_threads(max_values, N, tol, maxit);

  return 0;
}