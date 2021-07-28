#include "halo.h"
#include "basics.h"
#include "cosmo3D.h"
#include "recompute.h"
#include "redshift_spline.h"
#include "structs.h"
#include <assert.h>
#include <gsl/gsl_deriv.h>
#include <gsl/gsl_sf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.c/src/log.h"

static int GSL_WORKSPACE_SIZE = 250;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// halo model routines to convert scales/radii to halo masses, and vice versa
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double delta_c(double a __attribute__((unused))) 
{ // set to 1.686 for consistency with Tinker mass & bias definition
  return 1.686; 
}

double delta_Delta(double a __attribute__((unused))) 
{ // using Tinker et al mass & bias functions with \Delta = 200 rho_{mean}
  return 200.0;
}

double rho_Delta(const double a) // virial density in solar masses/h/(H0/c)^3 
{ // using Tinker et al mass & bias functions with \Delta = 200 rho_{mean}
  return (delta_Delta(a) * cosmology.rho_crit * cosmology.Omega_m);
}

double m_Delta(const double r, const double a) 
{
  return (4.0*M_PI /3.0)*r*r*r*rho_Delta(a);
}

double r_Delta(const double m, const double a) 
{ // calculate r_Delta in c/H0 given m in (solar masses/h)
  return pow(3./(4.0*M_PI)*(m/rho_Delta(a)), 1./3.);
}

double r_s(const double m, const double a) 
{ 
  return r_Delta(m, a) / conc(m, a); 
}

double radius(const double m) 
{
  return pow(3.0/4.0*m/(M_PI*cosmology.rho_crit*cosmology.Omega_m), 1.0/3.0);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Variance of the density field
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double int_for_sigma2(double x, void* params) // inner integral
{ // refactored FT of spherical top-hat to avoid numerica divergence of 1/x
  double* ar = (double*) params;
  const double k = x/ar[0];
  const double pk = p_lin(k, 1.0);
  
  gsl_sf_result J1;
  int status = gsl_sf_bessel_j1_e(x, &J1);
  if (status) 
  {
    log_fatal(gsl_strerror(status));
    exit(1);
  }
  const double tmp = 3.0*J1.val/ar[0];

  return pk*tmp*tmp/(ar[0] * 2.0 * M_PI * M_PI);
}

double sigma2(const double M) 
{
  static cosmopara C;
  static double* table_S2;
  
  const double lnmmin = log(limits.M_min/2.0);
  const double lnmmax = log(limits.M_max*2.0);
  const double dm = (lnmmax - lnmmin)/Ntable.N_S2;
  const double lnM = log(M);

  const double min = 0;
  const double max = 14.1;

  if (table_S2 == 0) 
  {
    table_S2 = (double*) malloc(sizeof(double)*Ntable.N_S2);
  } 
  if (recompute_cosmo3D(C)) 
  {
    {
      double ar[1] {radius(exp(lnmmin))};
      int_for_sigma2(min, (void*) ar); // init
    }
    #pragma omp parallel for
    for (int i=0; i<Ntable.N_S2; i++) 
    {
      double ar[1] {radius(exp(lnmmin + i*dm))};
      table_S2[i] = log(int_gsl_integrate_medium_precision(int_for_sigma2, (void*) ar, min, max, 
        NULL, GSL_WORKSPACE_SIZE));
    }
    update_cosmopara(&C);
  }
  return exp(interpol(table_S2, Ntable.N_S2, lnmmin, lnmmax, dm, lnM, 1.0, 1.0));
}

double nu(const double M, const double a) 
{
  static int init = 1;
  if ((cosmology.Omega_nu > 0) && init) 
  {
    log_fatal("halo.c does not support cosmologies with massive neutrinos");
    init = 0;
  }
  return delta_c(a)/(sqrt(sigma2(M))*growfac(a));
}

double nuv2(const double M, const double a, const double growfac) 
{
  static int init = 1;
  if ((cosmology.Omega_nu > 0) && init) 
  {
    log_fatal("halo.c does not support cosmologies with massive neutrinos");
    init = 0;
  }
  return delta_c(a)/(sqrt(sigma2(M))*growfac);
}

double int_for_dlognudlogm(double lnM, void* params __attribute__((unused))) 
{
  return log(nu(exp(lnM), 1.0));
}

double dlognudlogm(const double M) 
{
  static cosmopara C;
  static double* table_DS;

  const double lnmmin = log(limits.M_min/2.0);
  const double lnmmax = log(limits.M_max*2.0);
  const double dm = (lnmmax - lnmmin)/Ntable.N_DS;
  const double lnM = log(M);

  if (table_DS == 0) 
  {
    table_DS = (double*) malloc(sizeof(double)* Ntable.N_DS);
  }
  if (recompute_cosmo3D(C)) 
  {
    {
      const int i = 0;
      double result, abserr;
      gsl_function F;
      F.function = &int_for_dlognudlogm;
      F.params = 0;

      const double lnM_tmp = lnmmin + i*dm;
      int status = gsl_deriv_central(&F, lnM_tmp, 0.1*lnM_tmp, &result, &abserr);
      if (status) 
      {
        log_fatal(gsl_strerror(status));
        exit(1);
      }
      
      table_DS[i] = result;
    }
    #pragma omp parallel for
    for (int i=1; i <Ntable.N_DS; i++) 
    {
      double result, abserr;
      gsl_function F;
      F.function = &int_for_dlognudlogm;
      F.params = 0;

      const double lnM_tmp = lnmmin + i*dm;
      int status = gsl_deriv_central(&F, lnM_tmp, 0.1*lnM_tmp, &result, &abserr);
      if (status) 
      {
        log_fatal(gsl_strerror(status));
        exit(1);
      }

      table_DS[i] = result;
    }
    update_cosmopara(&C);
  }
  return interpol(table_DS, Ntable.N_DS, lnmmin, lnmmax, dm, lnM, 1.0, 1.0);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// mass function & halo bias (Tinker et al.)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double f_tinker(const double n, const double a_in) 
{ 
  // Eqs. (8-12) + Table 4 from Tinker et al. 2010
  // aa = alpha, b = beta, c = gamma, f = phi, e = eta
  double a = fmax(0.25, a_in); // limit fit range of mass function evolution to z <= 3, as
                               // discussed after Eq. 12 of http://arxiv.org/pdf/1001.3162v2.pdf
  const double aa = 0.368;
  const double b = 0.589 * pow(a, -0.2);
  const double c = 0.864 * pow(a, 0.01);
  const double f = -0.729 * pow(a, .08);
  const double e = -0.243 * pow(a, -0.27);
  return aa * (1.0 + pow(b * n, -2.0 * f)) * pow(n, 2.0 * e) * exp(-c * n * n / 2.0);
}

double fnu_tinker(const double n, const double a) 
{ 
  return f_tinker(n, a) * n; 
}

double B1_nu(const double n, const double a) 
{
  // Eq (6) + Table 2 from Tinker et al. 2010
  const double y = log10(200.0);
  const double A = 1.0 + 0.24 * y * exp(-pow(4.0 / y, 4.0));
  const double aa = 0.44 * y - 0.88;
  const double B = 0.183;
  const double b = 1.5;
  const double C = 0.019 + 0.107 * y + 0.19 * exp(-pow(4.0 / y, 4.0));
  const double c = 2.4;
  return 1.0 - A * pow(n, aa) / (pow(n, aa) + pow(delta_c(a), aa)) +
         B * pow(n, b) + C * pow(n, c);
}

// normalization for redshift evolution of mass function (Eq. (8) in
// Tinker et al. 2010), enforcing mass to be unbiased on average
double int_for_mass_norm(double n, void *params) 
{ // inner integral
  double* ar = (double*) params;
  const double a = ar[0];
  return f_tinker(n, a);
}

double mass_norm(const double a) 
{
  static cosmopara C;
  static double* table;
  
  const double amin = limits.a_min;
  const double amax = 1.;
  const double da = (amax - amin) / (Ntable.N_a - 1.);
  
  const double min = 0.0;
  const double max = 10.0;

  if (table == 0) 
  {
    table = (double*) malloc(sizeof(double)*Ntable.N_a);
  }
  if (recompute_cosmo3D(C)) 
  {
    {
      double ar[1] = {amin}; 
      int_for_mass_norm(min, (void*) ar); // init
    }
    #pragma omp parallel for
    for (int i=0; i<Ntable.N_a; i++) 
    {
      double ar[1] = {amin + i*da};      
      table[i] = int_gsl_integrate_medium_precision(int_for_mass_norm, (void*) ar, min, 
        max, NULL, GSL_WORKSPACE_SIZE);
    }
    update_cosmopara(&C);
  }
  return interpol(table, Ntable.N_a, amin, amax, da, fmin(a, amax-da), 1.0, 1.0);
}

double int_for_bias_norm(double n, void* params) 
{ // correct bias for to halo mass cuts (so large-scale 2-h matches PT results at all zs)
  double* ar = (double*) params;
  double a = ar[0];
  return B1_nu(n, a) * f_tinker(n, a);
}

double bias_norm(const double a) 
{
  static cosmopara C;
  static double* table;

  const int na = Ntable.N_a;
  const double amin = limits.a_min; 
  const double amax = 1.;
  const double da = (amax - amin) / (na - 1.);

  if (table == 0) 
  {
    table = (double*) malloc(sizeof(double)*Ntable.N_a);
  }
  if (recompute_cosmo3D(C)) 
  { 
    {
      double ar[1] = {amin}; 
      int_for_bias_norm(limits.M_min, (void*) ar); // init 
      nu(limits.M_min, amin); // init
    }
    #pragma omp parallel for
    for (int i=0; i<na-1; i++) 
    {
      double ar[1] = {amin + i*da};      
      table[i] = int_gsl_integrate_medium_precision(int_for_bias_norm, (void*) ar, 
        nu(limits.M_min, amin + i*da), nu(limits.M_max, amin + i*da), NULL, GSL_WORKSPACE_SIZE);
    }
    table[na - 1] = 1.0;
    update_cosmopara(&C);
  }
  return interpol(table, na, amin, amax, da, fmin(a, amax - da), 1.0, 1.0);
}

double massfunc(const double m, const double a) 
{
  return fnu_tinker(nu(m, a), a) * cosmology.rho_crit * cosmology.Omega_m/m/m * dlognudlogm(m);
}

double B1(const double m, const double a) 
{ // b(m,a) based on redshift evolution fits in Tinker et al. paper, no additional normalization
  return B1_nu(nu(m, a), a); 
}

double B1_normalized(const double m, const double a) 
{ // divide by bias norm only in matter spectra, not in HOD modeling/cluster analyses
  return B1_nu(nu(m, a), a)/bias_norm(a);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// halo profiles
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double conc(const double m, const double a) 
{ // mass-concentration relation: Bhattacharya et al. 2013, Delta = 200 rho_{mean} (Table 2)
  const double g = growfac(a);
  return 9.0*pow(nuv2(m, a, g), -0.29)*pow(g, 1.15); 
}

double u_nfw_c(const double c, const double k, const double m, const double a) 
{ // analytic FT of NFW profile, from Cooray & Sheth 01
  const double x = k * r_Delta(m, a) / c;
  const double xu = (1. + c) * x;

  gsl_sf_result SI_XU;
  int status = gsl_sf_Si_e(xu, &SI_XU);
  if (status) 
  {
    log_fatal(gsl_strerror(status));
    exit(1);
  }

  gsl_sf_result SI_X;
  {
    int status = gsl_sf_Si_e(x, &SI_X);
    if (status) 
    {
      log_fatal(gsl_strerror(status));
      exit(1);
    }
  }

  gsl_sf_result CI_XU;
  {
    int status = gsl_sf_Ci_e(xu, &CI_XU);
    if (status) 
    {
      log_fatal(gsl_strerror(status));
      exit(1);
    }
  }

  gsl_sf_result CI_X;
  {
    int status = gsl_sf_Ci_e(x, &CI_X);
    if (status) 
    {
      log_fatal(gsl_strerror(status));
      exit(1);
    }
  }

  return (sin(x)*(SI_XU.val - SI_X.val) - sinl(c*x)/xu + cos(x)*(CI_XU.val - CI_X.val))/
    (log(1. + c) - c/(1. + c));
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// matter power spectrum
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double int_for_I02(double logm, void* params) 
{
  double* ar = (double*) params;
  const int j = (int) ar[5];
  const double a = ar[0];
  const double k1 = ar[1];
  const double k2 = ar[2];
  const double m = exp(logm);
  const double c = conc_noiterp(m, a);
  const double u = u_nfw_c(c, k1, m, a)*u_nfw_c(c, k2, m, a);
  const double tmp = m/(cosmology.rho_crit * cosmology.Omega_m);
  return massfunc(m, a) * m * tmp * tmp * u;
}

double I02_noiterp(const double k1, const double k2, const double a) 
{
  double ar[2] = {a, k1, k2};
  return int_gsl_integrate_medium_precision(int_for_I02, (void*) ar, log(limits.M_min),
    log(limits.M_max), NULL, GSL_WORKSPACE_SIZE);
}

double int_for_I11(double logm, void* params) 
{
  const double* ar = (double*) params;  
  const double a = ar[0];
  const double k = ar[1];
  const double m = exp(logm);
  const double c = conc(m, a);
  const double u = u_nfw_c(c, k, m, a);
  return massfunc(m, a) * m * m * u * B1_normalized(m, a) / (cosmology.rho_crit * cosmology.Omega_m);
}

double I11_noiterp(const double k, const double a) 
{ 
   double ar[2] = {a, k};
   return int_gsl_integrate_medium_precision(int_for_I11, (void*) ar, log(limits.M_min), 
    log(limits.M_max), NULL, GSL_WORKSPACE_SIZE);
}

double I11(const double k, const double a) 
{ // look-up table for I11 integral
  static cosmopara C;
  static double** table_I1 = 0;

  const double amin = limits.a_min;
  const double amax = 1.0;
  const double da = (amax - amin) / (Ntable.N_a);
  const double logkmin = log(limits.k_min_cH0);
  const double logkmax = log(limits.k_max_cH0);
  const double dk = (logkmax - logkmin) / (Ntable.N_k_nlin);

  if (table_I1 == 0)
  {
    table_I1 = (double**) malloc(sizeof(double*)*Ntable.N_a);
    for(int i=0; i<Ntable.N_a;i++)
    {
      table_I1[i] = (double*) malloc(sizeof(double)*Ntable.N_k_nlin);
    }
  }
  if (recompute_cosmo3D(C)) 
  {
    {
      const int i = 0;
      {
        const int j = 0;
        table_I1[i][j] = I_11_noiterp(exp(logkmin + j*dk), fmin(amin + i*da, 0.999));
      }
      #pragma omp parallel for
      for (int j=1; j<Ntable.N_k_nlin; j++) 
      {
        table_I1[i][j] = I_11_noiterp(exp(logkmin + j*dk), fmin(amin + i*da, 0.999));
      }
    }
    #pragma omp parallel for collapse(2)
    for (int i=1; i<Ntable.N_a; i++) 
    {
      for (int j=0; j<Ntable.N_k_nlin; j++) 
      {
        table_I1[i][j] = I_11_noiterp(exp(logkmin + j*dk), fmin(amin + i*da, 0.999));
      }
    }
    update_cosmopara(&C);
  }
  const double tmp = fmin(a, amax - 1.1 * da); // to avoid interpolation errors near z = 0
  return exp(interpol2d(table_I1, Ntable.N_a, amin, amax, da, tmp, Ntable.N_k_nlin, 
    logkmin, logkmax, dk, log(k), 1.0, 1.0));
}

double p_mm_1h_noiterp(const double k, const double a) 
{ // 1Halo Terms
  return I02_noiterp(k, k, a); 
}

double p_mm_2h_noiterp(const double k, const double a) 
{ // 2Halo Terms
  const double tmp = I11_noiterp(k, a);
  return tmp * tmp * p_lin(k, a); 
}

double p_mm_halomodel(const double k, const double a)
{ 
  static cosmopara C;
  static double** table_P_NL = 0;

  const double da = (1.0 - limits.a_min) / ((double) Ntable.N_a);
  const double logkmin = log(limits.k_min_cH0);
  const double logkmax = log(limits.k_max_cH0);
  const double dk = (logkmax - logkmin) / ((double) Ntable.N_k_nlin);

  if (table_P_NL == 0)
  {
    table_P_NL = (double**) malloc(sizeof(double*)*Ntable.N_a);
    for(int i=0; i<Ntable.N_a;i++)
    {
      table_P_NL[i] = (double*) malloc(sizeof(double)*Ntable.N_k_nlin);
    } 
  }
  if (recompute_cosmo3D(C)) 
  { // extend this by halo model parameters if these become part of the model
    {
      const int i = 0;
      const double aa = limits.a_min + i*da > 0.999 ? .999 : limits.a_min + i*da;
      {
        const int j = 0;
        const double kk = exp(logkmin + j*dk);
        table_P_NL[i][j] = log(p_mm_1h_noiterp(kk, aa) + p_mm_2h_noiterp(kk, aa));
      }
      #pragma omp parallel for
      for (int j=1; j<Ntable.N_k_nlin; j++) 
      {
        const double kk = exp(logkmin + j*dk);
        table_P_NL[i][j] = log(p_mm_1h_noiterp(kk, aa) + p_mm_2h_noiterp(kk, aa));
      }
    }
    #pragma omp parallel for collapse(2)
    for (int i=1; i<Ntable.N_a; i++) 
    { 
      for (int j=0; j<Ntable.N_k_nlin; j++) 
      {
        const double aa = limits.a_min + i*da > 0.999 ? .999 : limits.a_min + i*da;
        const double kk = exp(logkmin + j*dk);
        table_P_NL[i][j] = log(p_mm_1h_noiterp(kk, aa) + p_mm_2h_noiterp(kk, aa));
      }
    }
    update_cosmopara(&C);
  }
  return exp(interpol2d(table_P_NL, Ntable.N_a, limits.a_min, 1.0, da, a, Ntable.N_k_nlin, logkmin, 
    logkmax, dk, log(k), 0.0, 0.0));
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// simplistic abundance matching to get approximate b(z) of source galaxies
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

double n_s_cmv(double a) 
{
  double dV_dz = pow(f_K(chi(a)), 2.0) / hoverh0(a); // comoving dV/dz(z = 1./a-1) per radian^2
  // dN/dz/radian^2/(dV/dz/radian^2)
  return zdistr_photoz(1.0/a - 1., -1) * survey.n_gal * survey.n_gal_conversion_factor / dV_dz; 
}

double int_for_ngmatched(double logm, void* params) 
{
  double* ar = (double*) params;
  const double a = ar[0];
  const double m = exp(logm);
  return massfunc(m, a) * m;
}

double int_for_b_ngmatched(double logm, void* params) 
{
  double* ar = (double*) params;
  const double a = ar[0];
  return massfunc(m, a) * m * B1(exp(logm), a);
}

double b_ngmatched(double a, double n_cmv) 
{
  double ar[1] = {a};
  const double dm = 0.1;
  
  double mlim = log(5.e+14);
  
  double n = int_gsl_integrate_medium_precision(int_for_ngmatched, (void*) ar, mlim, 
    log(limits.M_max), NULL, GSL_WORKSPACE_SIZE);
  
  while (n < n_cmv) 
  {
    mlim -= dm;
    n += int_gsl_integrate_medium_precision(int_for_ngmatched, (void*) ar, mlim, mlim + dm, 
      NULL, GSL_WORKSPACE_SIZE);
  }

  return int_gsl_integrate_medium_precision(int_for_b_ngmatched, (void*) ar, mlim, 
    log(limits.M_max), NULL, GSL_WORKSPACE_SIZE) /n;
}

double b_source(double a) // lookup table for b1 of source galaxies
{ 
  static double* table_b;
  
  const double amin = limits.a_min;
  const double amax = 1. / (1. + redshift.shear_zdistrpar_zmin);
  const double da = (amax - amin) / (1. * Ntable.N_a);
  const double ngal = survey.n_gal;
  const double mag_lim = survey.m_lim;
  
  if (table_b == 0) 
  { // if survey/redshift distribution changes, hopefully one of these will change too uncertainty
    // in abundance matching is larger than change with cosmology, so save time and don't 
    // recompute if only cosmology changes
    table_b = (double*) malloc(sizeof(double)*Ntable.N_a);
    {
      const int i = 0;
      table_b[i] = b_ngmatched(amin + i*da, n_s_cmv(amin + i*da));
    }
    #pragma omp parallel for
    for (int i=1; i<Ntable.N_a; i++) 
    {
      table_b[i] = b_ngmatched(amin + i*da, n_s_cmv(amin + i*da));
    }
  }
  return interpol(table_b, Ntable.N_a, amin, amax, da, a, 1.0, 1.0);
}
