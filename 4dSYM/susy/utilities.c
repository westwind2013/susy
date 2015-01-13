// -----------------------------------------------------------------
// Dirac operator and other helper functions for the action and force
#include "susy_includes.h"
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Compute at each site all NUMLINK * (NUMLINK - 1) plaquette determinants
// counting both orientations
// Use tempmat1 and staple as temporary storage
void compute_plaqdet() {
  register int i;
  register site *s;
  int a, b;
  msg_tag *mtag0 = NULL, *mtag1 = NULL;
  su3_matrix_f tmat, *mat;

  for (a = XUP; a < NUMLINK; a++) {
    for (b = XUP; b < NUMLINK; b++) {
      if (a == b)
        continue;

      // gen_pt[0] is U_b(x+a), gen_pt[1] is U_a(x+b)
      mtag0 = start_gather_site(F_OFFSET(linkf[b]), sizeof(su3_matrix_f),
                                goffset[a], EVENANDODD, gen_pt[0]);
      mtag1 = start_gather_site(F_OFFSET(linkf[a]), sizeof(su3_matrix_f),
                                goffset[b], EVENANDODD, gen_pt[1]);

      // tempmat1 = Udag_b(x+a) Udag_a(x) = [U_a(x) U_b(x+a)]^dag
      wait_gather(mtag0);
      FORALLSITES(i, s) {
        mat = (su3_matrix_f *)(gen_pt[0][i]);
        mult_su3_nn_f(&(s->linkf[a]), mat, &tmat);
        su3_adjoint_f(&tmat, &(tempmat1[i]));
      }
      cleanup_gather(mtag0);

      // staple = U_a(x+b) Udag_b(x+a) Udag_a(x)
      // tmat = U_b(x) U_a(x+b) Udag_b(x+a) Udag_a(x) = P_ab
      wait_gather(mtag1);
      FORALLSITES(i, s) {
        mat = (su3_matrix_f *)(gen_pt[1][i]);
        mult_su3_nn_f(mat, &(tempmat1[i]), &(staple[i]));
        mult_su3_nn_f(&(s->linkf[b]), &(staple[i]), &tmat);
        plaqdet[a][b][i] = find_det(&tmat);
      }
      cleanup_gather(mtag1);
    }
  }
}
// -----------------------------------------------------------------




// -----------------------------------------------------------------
// Compute at each site
//   sum_mu [U_mu(x) * Udag_mu(x) - Udag_mu(x - mu) * U_mu(x - mu)]
// Add plaquette determinant contribution if G is non-zero
// Use tempmat1, tempmat2 and staple as temporary storage
void compute_DmuUmu() {
  register int i;
  register site *s;
  int mu;
  msg_tag *mtag0 = NULL;
  su3_matrix_f *mat;

  if (G > IMAG_TOL)
    compute_plaqdet();      // Compute plaqdet if G is non-zero

  for (mu = XUP; mu < NUMLINK; mu++) {
    FORALLSITES(i, s) {
      mult_su3_na_f(&(s->linkf[mu]), &(s->linkf[mu]), &(tempmat1[i]));
      mult_su3_an_f(&(s->linkf[mu]), &(s->linkf[mu]), &(tempmat2[i]));
    }

    // Gather tempmat2 from below
    mtag0 = start_gather_field(tempmat2, sizeof(su3_matrix_f),
                               goffset[mu] + 1, EVENANDODD, gen_pt[0]);
    wait_gather(mtag0);
    if (mu == 0) {
      FORALLSITES(i, s) {
        mat = (su3_matrix_f *)(gen_pt[0][i]);
        sub_su3_matrix_f(&(tempmat1[i]), mat, &(DmuUmu[i]));
      }
    }
    else {
      FORALLSITES(i, s) {
        mat = (su3_matrix_f *)(gen_pt[0][i]);
        sub_su3_matrix_f(&(tempmat1[i]), mat, &(staple[i]));
        add_su3_matrix_f(&(DmuUmu[i]), &(staple[i]), &(DmuUmu[i]));
      }
    }
    cleanup_gather(mtag0);
  }

  // Add plaquette determinant contribution if G is non-zero
  if (G > IMAG_TOL) {
    int nu, j;
    Real re, im;
    FORALLSITES(i, s) {
      for (mu = XUP; mu < NUMLINK; mu++) {
        for (nu = mu + 1; nu < NUMLINK; nu++) {
          re = plaqdet[mu][nu][i].real + plaqdet[nu][mu][i].real - 2.0;
          im = plaqdet[mu][nu][i].imag + plaqdet[nu][mu][i].imag;
          for (j = 0; j < NCOL; j++) {
            DmuUmu[i].e[j][j].real += G * re;
            DmuUmu[i].e[j][j].imag += G * im;
          }
        }
      }
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Compute at each site
//   U_mu(x) * U_mu(x + mu) - Udag_nu(x) * U_mu(x + nu)
// Use tempmat1 and tempmat2 as temporary storage
void compute_Fmunu() {
  register int i;
  register site *s;
  int mu, nu, index;
  msg_tag *mtag0 = NULL, *mtag1 = NULL;

  for (mu = 0; mu < NUMLINK; mu++) {
    for (nu = mu + 1; nu < NUMLINK; nu++) {
      index = plaq_index[mu][nu];
      mtag0 = start_gather_site(F_OFFSET(linkf[nu]), sizeof(su3_matrix_f),
                                goffset[mu], EVENANDODD, gen_pt[0]);
      mtag1 = start_gather_site(F_OFFSET(linkf[mu]), sizeof(su3_matrix_f),
                                goffset[nu], EVENANDODD, gen_pt[1]);
      wait_gather(mtag0);
      wait_gather(mtag1);
      FORALLSITES(i, s) {
        mult_su3_nn_f(&(s->linkf[mu]), (su3_matrix_f *)(gen_pt[0][i]),
                      &(tempmat1[i]));
        mult_su3_nn_f(&(s->linkf[nu]), (su3_matrix_f *)(gen_pt[1][i]),
                      &(tempmat2[i]));
        sub_su3_matrix_f(&(tempmat1[i]), &(tempmat2[i]), &(Fmunu[index][i]));
      }
      cleanup_gather(mtag0);
      cleanup_gather(mtag1);
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Compute at each site B_mu(x) = U_mu(x) * Udag_mu(x) - trace
// as well as traceBB[mu][nu](x) = tr[B_mu(x) B_nu(x)] (should be real)
#ifdef CORR
void compute_Bmu() {
  register int i;
  register site *s;
  int mu, nu, j;
  complex ctmp;
  su3_matrix_f tmat;

  // B_mu = U_mu Udag_mu - trace
  FORALLSITES(i, s) {
    for (mu = XUP; mu < NUMLINK; mu++) {
      mult_su3_na_f(&(s->linkf[mu]), &(s->linkf[mu]), &(s->B[mu]));
      ctmp = trace_su3_f(&(s->B[mu]));
      CDIVREAL(ctmp, (Real)NCOL, ctmp);
      for (j = 0; j < NCOL; j++)
        CDIF(s->B[mu].e[j][j], ctmp);
    }

    // traceBB[mu][nu] = tr[B_mu(x) B_nu(x)] is symmetric in mu <--> nu
    // But store all to simplify SUGRA computation
    // Make sure Tr(B_a * B_b) is purely real
    for (mu = XUP; mu < NUMLINK; mu++) {
      for (nu = mu; nu < NUMLINK; nu++) {
        mult_su3_nn_f(&(s->B[mu]), &(s->B[nu]), &tmat);
        ctmp = trace_su3_f(&tmat);
        if (fabs(ctmp.imag) > IMAG_TOL) {
          printf("node%d WARNING: Tr(BB[%d][%d]) = (%.4g, %.4g) at site %d\n",
                 this_node, mu, nu, ctmp.real, ctmp.imag, i);
        }
        s->traceBB[mu][nu] = ctmp.real;
        s->traceBB[nu][mu] = ctmp.real;
      }
    }
  }

  // Alternative C_mu = U_mu + Udag_mu - trace
  FORALLSITES(i, s) {
    for (mu = XUP; mu < NUMLINK; mu++) {
      su3_adjoint_f(&(s->linkf[mu]), &tmat);
      add_su3_matrix_f(&(s->linkf[mu]), &tmat, &(s->C[mu]));
      scalar_mult_su3_matrix_f(&(s->C[mu]), 0.5, &(s->C[mu]));
      ctmp = trace_su3_f(&(s->C[mu]));
      CDIVREAL(ctmp, (Real)NCOL, ctmp);
      for (j = 0; j < NCOL; j++)
        CDIF(s->C[mu].e[j][j], ctmp);
    }

    // traceCC[mu][nu] = tr[C_mu(x) C_nu(x)] is symmetric in mu <--> nu
    // But store all to simplify SUGRA computation
    // Make sure Tr(C_a * C_b) is purely real
    for (mu = XUP; mu < NUMLINK; mu++) {
      for (nu = mu; nu < NUMLINK; nu++) {
        mult_su3_nn_f(&(s->C[mu]), &(s->C[nu]), &tmat);
        ctmp = trace_su3_f(&tmat);
        if (fabs(ctmp.imag) > IMAG_TOL) {
          printf("node%d WARNING: Tr(CC[%d][%d]) = (%.4g, %.4g) at site %d\n",
                 this_node, mu, nu, ctmp.real, ctmp.imag, i);
        }
        s->traceCC[mu][nu] = ctmp.real;
        s->traceCC[nu][mu] = ctmp.real;
      }
    }
  }
}
#endif
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Twist_Fermion matrix--vector operation
// Applies either the operator (sign = 1) or its adjoint (sign = -1)
void fermion_op(Twist_Fermion *src, Twist_Fermion *dest, int sign) {
  register int i, j;
  register site *s;
  int mu;
  complex detG, tc;
  Twist_Fermion tf;

  // Copy src TwistFermion into fieldwise site, link and plaq fermions
  // All of the latter are overwritten -- don't need to clear explicitly
  if (sign == 1) {
    FORALLSITES(i, s) {
      for (mu = 0; mu < NPLAQ; mu++)
        plaq_src[mu][i] = src[i].Fplaq[mu];
      for (mu = 0; mu < NUMLINK; mu++)
        link_src[mu][i] = src[i].Flink[mu];

      site_src[i] = src[i].Fsite;
    }
  }
  else if (sign == -1) {
    FORALLSITES(i, s) {
      conjTF(&(src[i]), &tf);
      for (mu = 0; mu < NPLAQ; mu++)
        plaq_src[mu][i] = tf.Fplaq[mu];
      for (mu = 0; mu < NUMLINK; mu++)
        link_src[mu][i] = tf.Flink[mu];

      site_src[i] = tf.Fsite;
    }
  }
  else {
    node0_printf("Error: incorrect sign in fermion_op\n");
    exit(1);
  }

  // Now the fun begins
#ifdef VP
  Dplus(link_src, plaq_dest);             // Overwrites plaq_dest
  Dminus(plaq_src, link_dest);            // Overwrites link_dest
#endif

#ifdef SV
  DbplusStoL(site_src, link_dest2);       // Overwrites link_dest2
  FORALLSITES(i, s) {
    for (mu = XUP; mu < NUMLINK; mu++)
      scalar_mult_add_su3_vector(&(link_dest[mu][i]), &(link_dest2[mu][i]),
                                 0.5, &(link_dest[mu][i]));
  }

  // Site-to-link plaquette determinant contribution if G is non-zero
  // Negative sign is due to anti-commuting eta past psi
  // compute_plaqdet() has already been run for the gauge action...
  if (G > IMAG_TOL) {
    detG = cmplx(0.0, 0.5 * G * sqrt((Real)NCOL));
    detStoL(site_src, link_dest2);        // Overwrites link_dest2
    FORALLSITES(i, s) {
      for (mu = XUP; mu < NUMLINK; mu++)
        c_scalar_mult_sub_su3vec(&(link_dest[mu][i]), &detG,
                                 &(link_dest2[mu][i]));
    }
  }

  DbminusLtoS(link_src, site_dest);       // Overwrites site_dest
  FORALLSITES(i, s)
    scalar_mult_su3_vector(&(site_dest[i]), 0.5, &(site_dest[i]));

  // Link-to-site plaquette determinant contribution if G is non-zero
  if (G > IMAG_TOL) {
    detLtoS(link_src, tr_dest);         // Overwrites tr_dest
    FORALLSITES(i, s) {
      CMUL(tr_dest[i], detG, tc);
      CSUM(site_dest[i].c[DIMF - 1], tc);
    }
  }
#endif

#ifdef QCLOSED
  DbminusPtoP(plaq_src, plaq_dest2);    // Overwrites plaq_dest2
  FORALLSITES(i, s) {
    for (mu = 0; mu < NPLAQ; mu++) {
      scalar_mult_add_su3_vector(&(plaq_dest[mu][i]), &(plaq_dest2[mu][i]),
                                 0.5, &(plaq_dest[mu][i]));
    }
  }
  DbplusPtoP(plaq_src, plaq_dest2);     // Overwrites plaq_dest2
  FORALLSITES(i, s) {
    for (mu = 0; mu < NPLAQ; mu++) {
      scalar_mult_add_su3_vector(&(plaq_dest[mu][i]), &(plaq_dest2[mu][i]),
                                 0.5, &(plaq_dest[mu][i]));
    }
  }
#endif

  // Copy local plaquette, link and site fermions into dest TwistFermion
  if (sign == 1) {
    FORALLSITES(i, s) {
      for (mu = 0; mu < NPLAQ; mu++)
        dest[i].Fplaq[mu] = plaq_dest[mu][i];
      for (mu = XUP; mu < NUMLINK; mu++)
        dest[i].Flink[mu] = link_dest[mu][i];

      dest[i].Fsite = site_dest[i];
    }
  }
  else if (sign == -1) {
    FORALLSITES(i, s) {
      for (j = 0; j < DIMF; j++) {
        for (mu = 0; mu < NPLAQ; mu++)
          CNEGATE(plaq_dest[mu][i].c[j], tf.Fplaq[mu].c[j]);
        for (mu = XUP; mu < NUMLINK; mu++)
          CNEGATE(link_dest[mu][i].c[j], tf.Flink[mu].c[j]);
        CNEGATE(site_dest[i].c[j], tf.Fsite.c[j]);
      }
      conjTF(&tf, &(dest[i]));
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
void Dplus(su3_vector *src[NUMLINK], su3_vector *dest[NPLAQ]) {
  register int i;
  register site *s;
  int mu, nu, index;
  su3_vector vtmp1, vtmp2, vtmp3, vtmp4, *vec0, *vec2;
  su3_matrix *mat1, *mat3;
  msg_tag *mtag0 = NULL, *mtag1 = NULL, *mtag2 = NULL, *mtag3 = NULL;

  for (mu = 0; mu < NUMLINK; mu++) {
    for (nu = mu + 1; nu < NUMLINK; nu++) {
      index = plaq_index[mu][nu];
      mtag0 = start_gather_field(src[nu], sizeof(su3_vector),
                                 goffset[mu], EVENANDODD, gen_pt[0]);

      mtag1 = start_gather_site(F_OFFSET(link[mu]), sizeof(su3_matrix),
                                goffset[nu], EVENANDODD, gen_pt[1]);

      mtag2 = start_gather_field(src[mu], sizeof(su3_vector),
                                 goffset[nu], EVENANDODD, gen_pt[2]);

      mtag3 = start_gather_site(F_OFFSET(link[nu]), sizeof(su3_matrix),
                                goffset[mu], EVENANDODD, gen_pt[3]);

      wait_gather(mtag0);
      wait_gather(mtag1);
      wait_gather(mtag2);
      wait_gather(mtag3);
      FORALLSITES(i, s) {
        vec0 = (su3_vector *)(gen_pt[0][i]);
        mat1 = (su3_matrix *)(gen_pt[1][i]);
        vec2 = (su3_vector *)(gen_pt[2][i]);
        mat3 = (su3_matrix *)(gen_pt[3][i]);
        mult_su3_mat_vec(&(s->link[mu]), vec0, &vtmp1);
        scalar_mult_su3_vector(&vtmp1, s->bc1[mu], &vtmp1);

        mult_su3_vec_mat(&(src[nu][i]), mat1, &vtmp2);
        mult_su3_mat_vec(&(s->link[nu]), vec2, &vtmp3);
        scalar_mult_su3_vector(&vtmp3, s->bc1[nu], &vtmp3);

        mult_su3_vec_mat(&(src[mu][i]), mat3, &vtmp4);
        sub_su3_vector(&vtmp1, &vtmp2, &vtmp1);
        sub_su3_vector(&vtmp3, &vtmp4, &vtmp3);
        sub_su3_vector(&vtmp1, &vtmp3, &(dest[index][i]));   // Overwrite
      }
      cleanup_gather(mtag0);
      cleanup_gather(mtag1);
      cleanup_gather(mtag2);
      cleanup_gather(mtag3);
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Use tempvec[0] for temporary storage
void Dminus(su3_vector *src[NPLAQ], su3_vector *dest[NUMLINK]) {
  register int i;
  register site *s;
  int mu, nu, index;
  su3_vector vtmp1, vtmp2, vtmp3, *vec1, tvec;
  su3_matrix *mat0;
  msg_tag *mtag0 = NULL, *mtag1 = NULL;

  for (nu = 0; nu < NUMLINK; nu++) {
    FORALLSITES(i, s)
      clearvec(&(dest[nu][i]));   // Overwrite

    for (mu = 0; mu < NUMLINK; mu++) {
      if (mu == nu)
        continue;

      index = plaq_index[mu][nu];
      mtag0 = start_gather_site(F_OFFSET(link[mu]), sizeof(su3_matrix),
                                goffset[nu], EVENANDODD, gen_pt[0]);

      FORALLSITES(i, s) {
        if (mu > nu) {    // src is anti-symmetric under mu <--> nu
          scalar_mult_su3_vector(&(src[index][i]), -1.0, &tvec);
          mult_su3_vec_mat(&tvec, &(s->link[mu]), &(tempvec[0][i]));
        }
        else
          mult_su3_vec_mat(&(src[index][i]), &(s->link[mu]), &(tempvec[0][i]));
      }
      mtag1 = start_gather_field(tempvec[0], sizeof(su3_vector),
                                 goffset[mu] + 1, EVENANDODD, gen_pt[1]);

      wait_gather(mtag0);
      wait_gather(mtag1);
      FORALLSITES(i, s) {
        mat0 = (su3_matrix *)(gen_pt[0][i]);
        vec1 = (su3_vector *)(gen_pt[1][i]);
        if (mu > nu) {    // src is anti-symmetric under mu <--> nu
          scalar_mult_su3_vector(&(src[index][i]), -1.0, &tvec);
          mult_su3_mat_vec(mat0, &tvec, &vtmp1);
        }
        else
          mult_su3_mat_vec(mat0, &(src[index][i]), &vtmp1);
        scalar_mult_su3_vector(vec1, s->bc1[OPP_LDIR(mu)], &vtmp3);
        sub_su3_vector(&vtmp1, &vtmp3, &vtmp2);
        add_su3_vector(&(dest[nu][i]), &vtmp2, &(dest[nu][i]));
      }
      cleanup_gather(mtag0);
      cleanup_gather(mtag1);
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
void DbplusPtoP(su3_vector *src[NPLAQ], su3_vector *dest[NPLAQ]) {
  register int i;
  register site *s;
  char **local_pt[2][4];
  int a, b, c, d, e, j, gather, next, flip = 0, i_ab, i_de;
  Real permm;
  su3_vector *vec1, *vec2, vtmp1, vtmp2, vtmp3;
  su3_matrix *mat0, *mat3;
  msg_tag *tag0[2], *tag1[2], *tag2[2], *tag3[2];

  for (a = 0; a < NPLAQ; a++) {
    FORALLSITES(i, s)
      clearvec(&(dest[a][i]));   // Overwrite
  }

  for (a = 0; a < 4; a++) {
    local_pt[0][a] = gen_pt[a];
    local_pt[1][a] = gen_pt[4 + a];
  }

  // Start first set of gathers
  // From setup_lambda.c, we see b > a and e > d
  c = DbplusPtoP_lookup[0][2];
  d = DbplusPtoP_lookup[0][3];
  e = DbplusPtoP_lookup[0][4];
  i_de = plaq_index[d][e];
  tag0[0] = start_gather_site(F_OFFSET(link[c]), sizeof(su3_matrix),
                              DbpP_d1[0], EVENANDODD, local_pt[0][0]);
  tag1[0] = start_gather_field(src[i_de], sizeof(su3_vector),
                               DbpP_d2[0], EVENANDODD, local_pt[0][1]);
  tag2[0] = start_gather_field(src[i_de], sizeof(su3_vector),
                               DbpP_d1[0], EVENANDODD, local_pt[0][2]);
  tag3[0] = start_gather_site(F_OFFSET(link[c]), sizeof(su3_matrix),
                              goffset[c] + 1, EVENANDODD, local_pt[0][3]);

  // Loop over lookup table
  for (j = 0; j < NTERMS; j++) {
    gather = (flip + 1) % 2;
    if (j < NTERMS - 1) {               // Start next set of gathers
      next = j + 1;
      c = DbplusPtoP_lookup[next][2];
      d = DbplusPtoP_lookup[next][3];
      e = DbplusPtoP_lookup[next][4];
      i_de = plaq_index[d][e];

      tag0[gather] = start_gather_site(F_OFFSET(link[c]),
                              sizeof(su3_matrix), DbpP_d1[next], EVENANDODD,
                              local_pt[gather][0]);
      tag1[gather] = start_gather_field(src[i_de],
                              sizeof(su3_vector), DbpP_d2[next], EVENANDODD,
                              local_pt[gather][1]);
      tag2[gather] = start_gather_field(src[i_de],
                              sizeof(su3_vector), DbpP_d1[next], EVENANDODD,
                              local_pt[gather][2]);
      tag3[gather] = start_gather_site(F_OFFSET(link[c]),
                              sizeof(su3_matrix), goffset[c] + 1, EVENANDODD,
                              local_pt[gather][3]);
    }

    // Do this set of computations while next set of gathers runs
    a = DbplusPtoP_lookup[j][0];
    b = DbplusPtoP_lookup[j][1];
    c = DbplusPtoP_lookup[j][2];
    d = DbplusPtoP_lookup[j][3];
    e = DbplusPtoP_lookup[j][4];
    permm = perm[a][b][c][d][e];
    i_ab = plaq_index[a][b];

    wait_gather(tag0[flip]);
    wait_gather(tag1[flip]);
    wait_gather(tag2[flip]);
    wait_gather(tag3[flip]);
    FORALLSITES(i, s) {
      mat0 = (su3_matrix *)(local_pt[flip][0][i]);
      vec1 = (su3_vector *)(local_pt[flip][1][i]);
      vec2 = (su3_vector *)(local_pt[flip][2][i]);
      mat3 = (su3_matrix *)(local_pt[flip][3][i]);
      mult_su3_vec_adj_mat(vec1, mat0, &vtmp1);
      scalar_mult_su3_vector(&vtmp1, s->bc3[a][b][c], &vtmp1);

      mult_adj_su3_mat_vec(mat3, vec2, &vtmp2);
      scalar_mult_su3_vector(&vtmp2, s->bc2[a][b], &vtmp2);

      sub_su3_vector(&vtmp1, &vtmp2, &vtmp3);
      scalar_mult_add_su3_vector(&(dest[i_ab][i]), &vtmp3,
                                 permm, &(dest[i_ab][i]));
    }
    cleanup_gather(tag0[flip]);
    cleanup_gather(tag1[flip]);
    cleanup_gather(tag2[flip]);
    cleanup_gather(tag3[flip]);
    flip = (flip + 1) % 2;
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
void DbminusPtoP(su3_vector *src[NPLAQ], su3_vector *dest[NPLAQ]) {
  register int i;
  register site *s;
  char **local_pt[2][4];
  int a, b, c, d, e, j, gather, next, flip = 0, i_ab, i_de;
  Real permm;
  su3_vector *vec1, *vec2, vtmp1, vtmp2, vtmp3;
  su3_matrix *mat0, *mat3;
  msg_tag *tag0[2], *tag1[2], *tag2[2], *tag3[2];

  for (d = 0; d < NPLAQ; d++) {
    FORALLSITES(i, s)
      clearvec(&(dest[d][i]));   // Overwrite
  }

  for (a = 0; a < 4; a++) {
    local_pt[0][a] = gen_pt[a];
    local_pt[1][a] = gen_pt[4 + a];
  }

  // Start first set of gathers
  // From setup_lambda.c, we see b > a and e > d
  a = DbminusPtoP_lookup[0][0];
  b = DbminusPtoP_lookup[0][1];
  c = DbminusPtoP_lookup[0][2];
  i_ab = plaq_index[a][b];

  tag0[0] = start_gather_site(F_OFFSET(link[c]), sizeof(su3_matrix),
                              DbmP_d1[0], EVENANDODD, local_pt[0][0]);
  tag1[0] = start_gather_field(src[i_ab], sizeof(su3_vector),
                               DbmP_d2[0], EVENANDODD, local_pt[0][1]);
  tag2[0] = start_gather_field(src[i_ab], sizeof(su3_vector),
                               DbmP_d1[0], EVENANDODD, local_pt[0][2]);
  tag3[0] = start_gather_site(F_OFFSET(link[c]), sizeof(su3_matrix),
                              goffset[c] + 1, EVENANDODD, local_pt[0][3]);

  // Loop over lookup table
  for (j = 0; j < NTERMS; j++) {
    gather = (flip + 1) % 2;
    if (j < NTERMS - 1) {               // Start next set of gathers
      next = j + 1;
      a = DbminusPtoP_lookup[next][0];
      b = DbminusPtoP_lookup[next][1];
      c = DbminusPtoP_lookup[next][2];
      i_ab = plaq_index[a][b];
      tag0[gather] = start_gather_site(F_OFFSET(link[c]),
                              sizeof(su3_matrix), DbmP_d1[next], EVENANDODD,
                              local_pt[gather][0]);
      tag1[gather] = start_gather_field(src[i_ab],
                              sizeof(su3_vector), DbmP_d2[next], EVENANDODD,
                              local_pt[gather][1]);
      tag2[gather] = start_gather_field(src[i_ab],
                              sizeof(su3_vector), DbmP_d1[next], EVENANDODD,
                              local_pt[gather][2]);
      tag3[gather] = start_gather_site(F_OFFSET(link[c]),
                              sizeof(su3_matrix), goffset[c] + 1, EVENANDODD,
                              local_pt[gather][3]);
    }

    // Do this set of computations while next set of gathers runs
    a = DbminusPtoP_lookup[j][0];
    b = DbminusPtoP_lookup[j][1];
    c = DbminusPtoP_lookup[j][2];
    d = DbminusPtoP_lookup[j][3];
    e = DbminusPtoP_lookup[j][4];
    permm = perm[a][b][c][d][e];
    i_de = plaq_index[d][e];

    wait_gather(tag0[flip]);
    wait_gather(tag1[flip]);
    wait_gather(tag2[flip]);
    wait_gather(tag3[flip]);
    FORALLSITES(i, s) {
      mat0 = (su3_matrix *)(local_pt[flip][0][i]);
      vec1 = (su3_vector *)(local_pt[flip][1][i]);
      vec2 = (su3_vector *)(local_pt[flip][2][i]);
      mat3 = (su3_matrix *)(local_pt[flip][3][i]);
      mult_su3_vec_adj_mat(vec1, mat0, &vtmp1);
      scalar_mult_su3_vector(&vtmp1, s->bc2[OPP_LDIR(a)][OPP_LDIR(b)],
                             &vtmp1);

      mult_adj_su3_mat_vec(mat3, vec2, &vtmp2);
      scalar_mult_su3_vector(&vtmp2,
                             s->bc3[OPP_LDIR(a)][OPP_LDIR(b)][OPP_LDIR(c)],
                             &vtmp2);

      sub_su3_vector(&vtmp1, &vtmp2, &vtmp3);
      scalar_mult_add_su3_vector(&(dest[i_de][i]), &vtmp3,
                                 permm, &(dest[i_de][i]));
    }
    cleanup_gather(tag0[flip]);
    cleanup_gather(tag1[flip]);
    cleanup_gather(tag2[flip]);
    cleanup_gather(tag3[flip]);
    flip = (flip + 1) % 2;
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
void DbplusStoL(su3_vector *src, su3_vector *dest[NUMLINK]) {
  register int i;
  register site *s;
  int mu;
  su3_vector vtmp1, vtmp2, *vec;
  msg_tag *tag[NUMLINK];

  tag[0] = start_gather_field(src, sizeof(su3_vector),
                              goffset[0], EVENANDODD, gen_pt[0]);
  for (mu = 0; mu < NUMLINK; mu++) {
    if (mu < NUMLINK - 1)     // Start next gather
      tag[mu + 1] = start_gather_field(src, sizeof(su3_vector),
                                       goffset[mu + 1], EVENANDODD,
                                       gen_pt[mu + 1]);

    wait_gather(tag[mu]);
    FORALLSITES(i, s) {
      vec = (su3_vector *)(gen_pt[mu][i]);
      mult_su3_vec_adj_mat(vec, &(s->link[mu]), &vtmp1);
      scalar_mult_su3_vector(&vtmp1, s->bc1[mu], &vtmp1);
      mult_adj_su3_mat_vec(&(s->link[mu]), &(src[i]), &vtmp2);
      sub_su3_vector(&vtmp1, &vtmp2, &(dest[mu][i]));   // Overwrite
    }
    cleanup_gather(tag[mu]);
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// If G is non-zero, the plaquette determinant couples the site
// Tr[eta] = i * sqrt(N) src[DIMF - 1] to the link dest
// The specific coupling for dest[b][j] is
//   -0.5G sum_{a, b} [det P_{ab} * Tr(U_b^{-1} Lambda[j])] Tr[eta]
// and similarly for dest[a][j] at x + b
// Negative sign is due to anti-commuting eta past psi
// Use tempvec and Tr_Uinv[0] for temporary storage
void detStoL(su3_vector *src, su3_vector *dest[NUMLINK]) {
  register int i;
  register site *s;
  int a, b, j;
  complex tc1, tc2;
  su3_matrix_f tmat1, tmat2;
  msg_tag *mtag;

  // Save Tr[U_a^{-1} Lambda[j]] in tempvec[a]
  for (a = XUP; a < NUMLINK; a++) {
    FORALLSITES(i, s) {
      clearvec(&(dest[a][i]));   // Overwrite

      invert(&(s->linkf[a]), &tmat1);
      for (j = 0; j < DIMF; j++) {
        mult_su3_nn_f(&tmat1, &(Lambda[j]), &tmat2);
        tempvec[a][i].c[j] = trace_su3_f(&tmat2);
      }
    }
  }

  for (a = XUP; a < NUMLINK; a++) {
    for (b = XUP; b < NUMLINK; b++) {
      if (a == b)
        continue;

      // Gather Tr[eta] det P_{ab} from x - b
      FORALLSITES(i, s)
        CMUL(src[i].c[DIMF - 1], plaqdet[a][b][i], Tr_Uinv[0][i]);
      mtag = start_gather_field(Tr_Uinv[0], sizeof(complex),
                                goffset[b] + 1, EVENANDODD, gen_pt[0]);

      // Compute on-site contribution to dest[b] while gather runs
      FORALLSITES(i, s) {
        CMULREAL(Tr_Uinv[0][i], s->bc1[b], tc1);
        c_scalar_mult_add_su3vec(&(dest[b][i]), &tc1, &(tempvec[b][i]));
      }

      // Now compute contribution to dest[a] at x + b
      wait_gather(mtag);
      FORALLSITES(i, s) {
        tc1 = *((complex *)(gen_pt[0][i]));
        CMULREAL(tc1, s->bc1[a], tc2);
        c_scalar_mult_add_su3vec(&(dest[a][i]), &tc2, &(tempvec[a][i]));
      }
      cleanup_gather(mtag);
    }
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// Term in action connecting the link fermions to a site fermion
// Given src psi_a, dest is Dbar_a psi_a (Eq. 63 in the arXiv:1108.1503)
// Use tempvec for temporary storage
void DbminusLtoS(su3_vector *src[NUMLINK], su3_vector *dest) {
  register int i;
  register site *s;
  int mu;
  su3_vector vtmp1, vtmp2, vtmp3, *vec;
  msg_tag *tag[NUMLINK];

  FORALLSITES(i, s) {         // Set up first gather
    clearvec(&(dest[i]));     // Overwrite
    mult_adj_su3_mat_vec(&(s->link[0]), &(src[0][i]), &(tempvec[0][i]));
  }
  tag[0] = start_gather_field(tempvec[0], sizeof(su3_vector),
                              goffset[0] + 1, EVENANDODD, gen_pt[0]);

  for (mu = 0; mu < NUMLINK; mu++) {
    if (mu < NUMLINK - 1) {   // Start next gather
      FORALLSITES(i, s)
        mult_adj_su3_mat_vec(&(s->link[mu + 1]), &(src[mu + 1][i]),
                             &(tempvec[mu + 1][i]));

      tag[mu + 1] = start_gather_field(tempvec[mu + 1], sizeof(su3_vector),
                                       goffset[mu + 1] + 1, EVENANDODD,
                                       gen_pt[mu + 1]);
    }

    wait_gather(tag[mu]);
    FORALLSITES(i, s) {
      vec = (su3_vector *)(gen_pt[mu][i]);
      mult_su3_vec_adj_mat(&(src[mu][i]), &(s->link[mu]), &vtmp1);
      scalar_mult_su3_vector(vec, s->bc1[OPP_LDIR(mu)], &vtmp3);
      sub_su3_vector(&vtmp1, &vtmp3, &vtmp2);
      add_su3_vector(&(dest[i]), &vtmp2, &(dest[i]));
    }
    cleanup_gather(tag[mu]);
  }
}
// -----------------------------------------------------------------



// -----------------------------------------------------------------
// If G is non-zero, the plaquette determinant couples the link src
// to the site Tr[eta] = i * sqrt(N) dest[DIMF - 1]
// The specific coupling is
//   0.5G sum_{a, b} [det P_{ab} * Tr(U_b^{-1} psi_b + U_a^{-1} psi_a)]
// with the same sign as DbminusLtoS
void detLtoS(su3_vector *src[NUMLINK], complex *dest) {
  register int i;
  register site *s;
  int a, b, j;
  complex tc1, tc2, trace;
  su3_matrix_f tmat1, tmat2;
  msg_tag *mtag;

  FORALLSITES(i, s)
    dest[i] = cmplx(0.0, 0.0);              // Overwrite

  // Prepare Tr[U_a^{-1} psi_a] = sum_j Tr[U_a^{-1} Lambda^j] psi_a^j
  // and save in Tr_Uinv[a]
  for (a = XUP; a < NUMLINK; a++) {
    FORALLSITES(i, s) {
      invert(&(s->linkf[a]), &tmat1);
      trace = cmplx(0.0, 0.0);              // Initialize
      for (j = 0; j < DIMF; j++) {
        mult_su3_nn_f(&tmat1, &(Lambda[j]), &tmat2);
        tc1 = trace_su3_f(&tmat2);
        CMUL(tc1, src[a][i].c[j], tc2);
        CSUM(trace, tc2);                   // Accumulate
      }
      CMULREAL(trace, s->bc1[a], Tr_Uinv[a][i]);
    }
  }

  for (a = XUP; a < NUMLINK; a++) {
    for (b = XUP; b < NUMLINK; b++) {
      if (a == b)
        continue;

      // Gather Tr[U_a^{-1} psi_a] from x + b
      mtag = start_gather_field(Tr_Uinv[a], sizeof(complex),
                                goffset[b], EVENANDODD, gen_pt[0]);
      wait_gather(mtag);

      // Now put it all together into dest
      FORALLSITES(i, s) {
        tc1 = *((complex *)(gen_pt[0][i]));
        CADD(Tr_Uinv[b][i], tc1, tc2);
        CMUL(plaqdet[a][b][i], tc2, tc1);
        CSUM(dest[i], tc1);
      }
      cleanup_gather(mtag);
    }
  }
}
// -----------------------------------------------------------------
