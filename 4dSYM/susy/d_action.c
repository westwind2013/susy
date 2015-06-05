// -----------------------------------------------------------------
// Measure total action, as needed by the hybrid Monte Carlo algorithm
// When this routine is called the CG should already have been run,
// so that the vector **sol contains (M_adjoint*M+shift[n])^(-1) * src
#include "susy_includes.h"
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Gauge momentum contribution to the action
double d_hmom_action() {
  register int i, mu;
  register site *s;
  double sum = 0.0;

  for (mu = XUP; mu < NUMLINK; mu++) {
    FORALLSITES(i, s)
      sum += (double)realtrace_su3_f(&(s->mom[mu]), &(s->mom[mu]));
  }
  g_doublesum(&sum);
  return sum;
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Include tunable coefficient C2 in the d^2 term
double d_gauge_action(int do_det) {
  register int i;
  register site *s;
  int index;
  double g_action = 0.0, norm = 0.5 * C2;
  complex tc;
  su3_matrix_f tmat, tmat2;

  compute_DmuUmu();
  compute_Fmunu();
  FORALLSITES(i, s) {
    // d^2 term normalized by C2 / 2
    // DmuUmu includes the plaquette determinant contribution if G is non-zero
    // and the scalar potential contribution if B is non-zero
    mult_su3_nn_f(&(DmuUmu[i]), &(DmuUmu[i]), &tmat);
    scalar_mult_su3_matrix_f(&tmat, norm, &tmat);

    // F^2 term
    for (index = 0; index < NPLAQ; index++) {
      mult_su3_an_f(&(Fmunu[index][i]), &(Fmunu[index][i]), &tmat2);
      scalar_mult_add_su3_matrix_f(&tmat, &tmat2, 2.0, &tmat);
    }

    if (do_det == 1)
      det_project(&tmat, &tmat2);
    else
      su3mat_copy_f(&tmat, &tmat2);

    tc = trace_su3_f(&tmat2);
    g_action += tc.real;
#ifdef DEBUG_CHECK
    if (fabs(tc.imag) > IMAG_TOL)
      printf("node%d WARNING: Im[s_B[%d]] = %.4g\n", this_node, i, tc.imag);
#endif
  }
  g_action *= kappa;
  g_doublesum(&g_action);
  return g_action;
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Separate scalar potential contribution to the action
// Note factor of kappa
double d_bmass_action() {
  register int i;
  register site *s;
  int mu;
  double sum = 0.0, tr;

  FORALLSITES(i, s) {
    for (mu = XUP; mu < NUMLINK; mu++) {
      tr = 1.0 / (double)NCOL;
      tr *= realtrace_su3_f(&(s->linkf[mu]), &(s->linkf[mu]));
      tr -= 1.0;
      sum += kappa * bmass * bmass * tr * tr;
    }
  }
  g_doublesum(&sum);
  return sum;
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Separate plaquette determinant contribution to the action
double d_det_action() {
  register int i;
  register site *s;
  int a, b;
  double re, im, det_action = 0.0;

  compute_plaqdet();
  FORALLSITES(i, s) {
    for (a = XUP; a < NUMLINK; a++) {
      for (b = a + 1; b < NUMLINK; b++) {
        re = plaqdet[a][b][i].real;
        im = plaqdet[a][b][i].imag;
        det_action += (re - 1.0) * (re - 1.0);
        det_action += im * im;
      }
    }
  }
  det_action *= kappa_u1;
  g_doublesum(&det_action);
  return det_action;
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Fermion contribution to the action
// Include the ampdeg term to allow sanity check that the fermion action
// is 16 DIMF volume on average
// Since the pseudofermion src is fixed throughout the trajectory,
// ampdeg actually has no effect on Delta S (checked)
// sol, however, depends on the gauge fields through the CG
double d_fermion_action(Twist_Fermion *src, Twist_Fermion **sol) {
  register int i, j;
  register site *s;
  double sum = 0.0;
  complex ctmp;
#ifdef DEBUG_CHECK
  double im = 0.0;
#endif

  FORALLSITES(i, s) {
    sum += ampdeg4 * (double)magsq_TF(&(src[i]));
    for (j = 0; j < Norder; j++) {
      ctmp = TF_dot(&(src[i]), &(sol[j][i]));   // src^dag.sol[j]
      sum += (double)(amp4[j] * ctmp.real);
#ifdef DEBUG_CHECK  // Make sure imaginary part vanishes
      im += (double)(amp4[j] * ctmp.imag);
#endif
    }
  }
  g_doublesum(&sum);
#ifdef DEBUG_CHECK  // Make sure imaginary part vanishes
  g_doublesum(&im);
  node0_printf("S_f = (%.4g, %.4g)\n", sum, im);
#endif
  return sum;
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Print out zeros for pieces of the action that aren't included
double d_action(Twist_Fermion **src, Twist_Fermion ***sol) {
  double g_act, bmass_act = 0.0, h_act, f_act, det_act = 0.0;
  double total;

  g_act = d_gauge_action(NODET);
  if (bmass > IMAG_TOL)
    bmass_act = d_bmass_action();
  if (kappa_u1 > IMAG_TOL)
    det_act = d_det_action();

  node0_printf("action: ");
  node0_printf("gauge %.8g bmass %.8g ", g_act, bmass_act);
  node0_printf("det %.8g ", det_act);

  total = g_act + bmass_act + det_act;
#ifndef PUREGAUGE
  int n;
  for (n = 0; n < Nroot; n++) {
    f_act = d_fermion_action(src[n], sol[n]);
    node0_printf("fermion%d %.8g ", n, f_act);
    total += f_act;
  }
#endif
  h_act = d_hmom_action();
  node0_printf("hmom %.8g ", h_act);
  total += h_act;
  node0_printf("sum %.8g\n", total);
  return total;
}
// -----------------------------------------------------------------
