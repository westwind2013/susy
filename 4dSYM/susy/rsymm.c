// -----------------------------------------------------------------
// Modified rectangular Wilson loops of fundamental links
// Evaluate in different spatial dirs to check rotational invariance
// Checked that results are gauge invariant
#include "susy_includes.h"
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Walk around path of fundamental links specified by dir, sign and kind
// dir lists the directions in the path
// sign lists whether to go forward (1) or backwards (-1)
// kind tells us whether to use linkf (1) or mom = (linkf^{-1})^dag (-1)
// length is the length of the path, and of each array
// Uses tempmat1 to accumulate linkf product along path
void rsymm_path(int *dir, int *sign, int *kind, int length) {
  register int i;
  register site *s;
  int j;
  msg_tag *mtag0 = NULL;

  // Initialize tempmat1 with first link in path
  if (sign[0] > 0) {    // Gather forwards, no adjoint
    if (kind[0] > 0) {
      mtag0 = start_gather_site(F_OFFSET(linkf[dir[0]]), sizeof(su3_matrix_f),
                                goffset[dir[0]] + 1, EVENANDODD, gen_pt[0]);
    }
    else if (kind[0] < 0) {
      mtag0 = start_gather_site(F_OFFSET(mom[dir[0]]), sizeof(su3_matrix_f),
                                goffset[dir[0]] + 1, EVENANDODD, gen_pt[0]);
    }
    else {
      node0_printf("rsymm_path: unrecognized kind[0] = %d\n", kind[0]);
      terminate(1);
    }

    wait_gather(mtag0);
    FORALLSITES(i, s)
      su3mat_copy_f((su3_matrix_f *)(gen_pt[0][i]), &(s->tempmat1));
    cleanup_gather(mtag0);
  }

  else if (sign[0] < 0) {    // Take adjoint, no gather
    FORALLSITES(i, s) {
      if (kind[0] > 0)
        su3_adjoint_f(&(s->linkf[dir[0]]), &(s->tempmat1));
      else if (kind[0] < 0)
        su3_adjoint_f(&(s->mom[dir[0]]), &(s->tempmat1));
      else {
        node0_printf("rsymm_path: unrecognized kind[0] = %d\n", kind[0]);
        terminate(1);
      }
    }
  }
  else {
    node0_printf("rsymm_path: unrecognized sign[0] = %d\n", sign[0]);
    terminate(1);
  }

  // Accumulate subsequent links in product in tempmat1
  for (j = 1; j < length; j++) {
    if (sign[j] > 0) {    // mult_su3_nn_f then gather backwards
      FORALLSITES(i, s) {
        if (kind[j] > 0)
          mult_su3_nn_f(&(s->tempmat1), &(s->linkf[dir[j]]), &(s->tempmat2));
        else if (kind[j] < 0)
          mult_su3_nn_f(&(s->tempmat1), &(s->mom[dir[j]]), &(s->tempmat2));
        else {
          node0_printf("rsymm_path: unrecognized kind[%d] = %d\n", j, kind[j]);
          terminate(1);
        }
      }
      mtag0 = start_gather_site(F_OFFSET(tempmat2), sizeof(su3_matrix_f),
                                goffset[dir[j]] + 1, EVENANDODD, gen_pt[0]);

      wait_gather(mtag0);
      FORALLSITES(i, s)
        su3mat_copy_f((su3_matrix_f *)(gen_pt[0][i]), &(s->tempmat1));
      cleanup_gather(mtag0);
    }

    else if (sign[j] < 0) {    // Gather forwards then mult_su3_na_f
      mtag0 = start_gather_site(F_OFFSET(tempmat1), sizeof(su3_matrix_f),
                                goffset[dir[j]], EVENANDODD, gen_pt[1]);

      wait_gather(mtag0);
      FORALLSITES(i, s) {
        if (kind[j] > 0) {
          mult_su3_na_f((su3_matrix_f *)(gen_pt[1][i]), &(s->linkf[dir[j]]),
                        &(s->tempmat2));
        }
        else if (kind[j] < 0) {
          mult_su3_na_f((su3_matrix_f *)(gen_pt[1][i]), &(s->mom[dir[j]]),
                        &(s->tempmat2));
        }
        else {
          node0_printf("rsymm_path: unrecognized kind[%d] = %d\n", j, kind[j]);
          terminate(1);
        }
      }
      FORALLSITES(i, s)   // Don't want to overwrite tempmat1 too soon
        su3mat_copy_f(&(s->tempmat2), &(s->tempmat1));
      cleanup_gather(mtag0);
    }
    else {
      node0_printf("rsymm_path: unrecognized sign[%d] = %d\n", j, sign[j]);
      terminate(1);
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
void rsymm() {
  register int i;
  register site *s;
  int dir_normal, dir_inv, dist, dist_inv, mu, length;
  int rsymm_max = MAX_X + 1;    // Go out to L/2 x L/2 loops
  int dir[4 * rsymm_max], sign[4 * rsymm_max], kind[4 * rsymm_max];
  double wloop, invlink[NUMLINK], invlink_sum = 0.0;
  complex tc;
  su3_matrix_f tmat;

  node0_printf("rsymm: MAX = %d\n", rsymm_max);

  // Compute and optionally check inverse matrices
  // Temporarily store the adjoint of the inverse in momentum matrices,
  // since it transforms like the original link
  for (mu = XUP; mu < NUMLINK; mu++) {
    FORALLSITES(i, s) {
      invert(&(s->linkf[mu]), &tmat);
      su3_adjoint_f(&tmat, &(s->mom[mu]));

#ifdef DEBUG_CHECK
#define INV_TOL 1e-12
#define INV_TOL_SQ 1e-24
      // Check inversion -- tmat should be unit matrix
      int j, k;
      mult_su3_nn_f(&(s->mom[dir]), &(s->linkf[dir]), &tmat);
      for (j = 0; j < NCOL; j++) {
        if (fabs(1 - tmat.e[j][j].real) > INV_TOL
            || fabs(tmat.e[j][j].imag) > INV_TOL) {
          printf("Link inversion fails on node%d:\n", this_node);
          dumpmat_f(&tmat);
        }
        for (k = j + 1; k < NCOL; k++) {
          if (cabs_sq(&(tmat.e[j][k])) > INV_TOL_SQ
              || cabs_sq(&(tmat.e[k][j])) > INV_TOL_SQ) {
            printf("Link inversion fails on node%d:\n", this_node);
            dumpmat_f(&tmat);
          }
        }
      }
      // Check left multiplication in addition to right multiplication
      mult_su3_nn_f(&(s->linkf[dir]), &(s->mom[dir]), &tmat);
      for (j = 0; j < NCOL; j++) {
        if (fabs(1 - tmat.e[j][j].real) > INV_TOL
            || fabs(tmat.e[j][j].imag) > INV_TOL) {
          printf("Link inversion fails on node%d:\n", this_node);
          dumpmat_f(&tmat);
        }
        for (k = j + 1; k < NCOL; k++) {
          if (cabs_sq(&(tmat.e[j][k])) > INV_TOL_SQ
              || cabs_sq(&(tmat.e[k][j])) > INV_TOL_SQ) {
            printf("Link inversion fails on node%d:\n", this_node);
            dumpmat_f(&tmat);
          }
        }
      }
#endif
    }
  }

  // First check average value of the inverted link
  // Tr[U^{-1} (U^{-1})^dag] / N
  // Just like d_link() but use s->mom instead of s->linkf
    for (dir_inv = XUP; dir_inv < NUMLINK; dir_inv++) {
    invlink[dir_inv] = 0;
    FORALLSITES(i, s)
      invlink[dir_inv] += realtrace_su3_f(&(s->mom[dir_inv]),
                                          &(s->mom[dir_inv]))
                          / ((double)(NCOL));
    g_doublesum(&(invlink[dir_inv]));
  }

  node0_printf("INVLINK");
  for (dir_inv = XUP; dir_inv < NUMLINK; dir_inv++) {
    invlink[dir_inv] /= ((double)volume);
    invlink_sum += invlink[dir_inv];
    node0_printf(" %.6g", invlink[dir_inv]);
  }
  node0_printf(" %.6g\n", invlink_sum / ((double)(NUMLINK)));

  // Construct and print all loops up to rsymm_max x rsymm_max
  // in all NUMLINK * (NUMLINK - 1) directions
  // Invert all links in the second direction in each loop
  for (dir_normal = XUP; dir_normal < NUMLINK; dir_normal++) {
    for (dir_inv = XUP; dir_inv < NUMLINK; dir_inv++) {
      if (dir_inv == dir_normal)
        continue;

      for (dist = 1; dist <= rsymm_max; dist++) {
        for (dist_inv = 1; dist_inv <= rsymm_max; dist_inv++) {
          // Set up rectangular Wilson loop path as list of dir, sign * kind
          length = 2 * (dist + dist_inv);
          for (i = 0; i < dist; i++) {
            dir[i] = dir_normal;
            sign[i] = 1;
            kind[i] = 1;
          }
          for (i = dist; i < dist + dist_inv; i++) {
            dir[i] = dir_inv;
            sign[i] = 1;
            kind[i] = -1;
          }
          for (i = dist + dist_inv; i < 2 * dist + dist_inv; i++) {
            dir[i] = dir_normal;
            sign[i] = -1;
            kind[i] = 1;
          }
          for (i = 2 * dist + dist_inv; i < length; i++) {
            dir[i] = dir_inv;
            sign[i] = -1;
            kind[i] = -1;
          }
#ifdef DEBUG_CHECK
          node0_printf("path %d [%d] %d [%d] length %d: ",
                       dir_normal, dist, dir_inv, dist_inv, length);
          for (i = 0; i < length; i++)
            node0_printf(" (%d)*%d(%d) ", sign[i], dir[i], kind[i]);
          node0_printf("\n");
#endif

          // rsymm_path accumulates the product in tempmat1
          rsymm_path(dir, sign, kind, length);
          wloop = 0.0;
          FORALLSITES(i, s) {
            tmat = s->tempmat1;
            tc= trace_su3_f(&tmat);
            wloop += tc.real;
          }
          g_doublesum(&wloop);
          // Format: # normal, dir normal, # inverted, dir inverted, result
          node0_printf("RSYMM %d [%d] %d [%d] %.8g\n",
                       dist, dir_normal, dist_inv, dir_inv, wloop / volume);
        } // dist_inv
      } // dist
    } // dir_inv
  } // dir_normal
}
// -----------------------------------------------------------------
