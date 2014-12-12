// -----------------------------------------------------------------
// Konishi and SUGRA operators integrated over lattice volume
#include "susy_includes.h"

void blocked_ops(int Nstout, int block) {
  register int i;
  register site *s;
  int a, b, c, d, mu, nu, bl = 2;
  Real norm, tr, OK[NDIMS], OS[NDIMS][NDIMS]; // Konishi and SUGRA operators

  // Initialize Konishi and SUGRA operators
  // SUGRA will be symmetric by construction, so ignore nu <= mu
  for (mu = 0; mu < NDIMS; mu++) {
    OK[mu] = 0.0;
    for (nu = mu + 1; nu < NDIMS; nu++)
      OS[mu][nu] = 0.0;
  }

  // Compute at each site B_a = U_a Udag_a - volume average
  // as well as traceBB[mu][nu] = tr[B_mu(x) B_nu(x)]
  // Now stored in the site structure
  compute_Bmu();

  // Now sum operators over lattice volume
  FORALLSITES(i, s) {
    for (a = 0; a < NUMLINK; a++) {
      for (b = 0; b < NUMLINK; b++) {
        // Four possible Konishi operators, all fairly easy
        OK[0] += s->traceBB[a][b];
        OK[1] += s->traceCC[a][b];
        for (c = 0; c < NUMLINK; c++) {
          for (d = 0; d < NUMLINK; d++) {
            OK[2] += s->traceBB[a][b] * s->traceBB[c][d];
            OK[3] += s->traceCC[a][b] * s->traceCC[c][d];
          }
        }

        // Now SUGRA with mu--nu trace subtraction
        // Symmetric and traceless by construction so ignore nu <= mu
        for (mu = 0; mu < NDIMS ; mu++) {
          for (nu = mu + 1; nu < NDIMS ; nu++) {
            tr = P[mu][a] * P[nu][b] + P[nu][a] * P[mu][b];
            OS[mu][nu] += 0.5 * tr * s->traceBB[a][b];
          }
        }
      }
    }

  }
  OK[0] /= 5.0;   // Remove from site loop
  OK[1] /= 5.0;
  OK[2] /= 25.0;
  OK[3] /= 25.0;
  for (mu = 0; mu < NDIMS; mu++) {
    g_doublesum(&(OK[mu]));
    for (nu = mu + 1; nu < NDIMS; nu++)
      g_doublesum(&(OS[mu][nu]));
  }

  // Print out integrated operators
  // Have to divide by number 2^(4block) = bl^4 of blocked lattices
  for (a = 1; a < block; a++)
    bl *= 2;
  if (block <= 0)
    bl = 1;
  norm = (Real)(bl * bl * bl * bl);
  node0_printf("OK %d %d", Nstout, block);
  for (mu = 0; mu < NDIMS; mu++)
    node0_printf(" %.8g", OK[mu] / norm);
  node0_printf("\n");

  // SUGRA, averaging over six components with mu < nu
  norm = (Real)(6.0 * bl * bl * bl * bl);
  tr = 0.0;
  for (mu = 0; mu < NDIMS; mu++) {
    for (nu = mu + 1; nu < NDIMS; nu++)
      tr += OS[mu][nu];
  }
  node0_printf("OS %d %d %.8g\n", Nstout, block, tr / norm);
}
// -----------------------------------------------------------------
