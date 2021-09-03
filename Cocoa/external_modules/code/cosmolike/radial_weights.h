#ifndef __COSMOLIKE_RADIAL_WEIGHTS_H
#define __COSMOLIKE_RADIAL_WEIGHTS_H
#ifdef __cplusplus
extern "C" {
#endif

// complete lens efficiency weight
double W_kappa(double a, double fK, int nz);

double W_mag(double a, double fK, int nz);

double W_mag_cluster(double a, double fK, int nz, int nl);

double W_gal(double a, int nz, double chi, double hoverh0);

// source redshift distribution (radial weight for IA,source clustering)
double W_source(double a, int nz, double hoverh0);

double f_rsd(double aa);

double W_RSD(double l, double a0, double a1, int nz);

double W_HOD(double a, int nz, double hoverh0);

double W_cluster(int nz, double a, double hoverh0);

double W_k(double a, double fK);

#ifdef __cplusplus
}
#endif
#endif // HEADER GUARD