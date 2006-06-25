#include <stddef.h>
#include <stdio.h>      
#include <string.h>
#include <math.h>
#include <Rmath.h>
#include <R.h>
#include "vector.h"
#include "subroutines.h"
#include "rand.h"

/* A random walk Metroplis sampler for logistic regression (binomial
   and multinomial) with normal prior (same and independent prior for
   each set of coefficients): the proposal distribution is
   multivariate normal whose mean is the current value and the
   variance is given by the input.
*/
void logitMetro(int *Y,        /* outcome variable: 0, 1, ..., J-1 */
		double **X,    /* (N x K) covariate matrix */
		double *beta,  /* (K(J-1)) stacked coefficient vector */
		int n_samp,    /* # of obs */
		int n_dim,     /* # of categories, J-1 */
		int n_cov,     /* # of covariates, K */
		double *beta0, /* (K(J-1)) prior mean vector */
		double **A0,   /* (K(J-1) x K(J-1)) prior precision */
		double *Var,   /* K(J-1) proposal variances */
		int n_gen,     /* # of MCMC draws */
		int *counter   /* # of acceptance for each parameter */
		) {
  
  int i, j, k, main_loop, param;
  double numer, denom;
  double sumall, sumall1, dtemp, dtemp1;
  double *prop = doubleArray(n_dim*n_cov);

  for (j = 0; j < n_cov*n_dim; j++)
    prop[j] = beta[j];

  for (main_loop = 0; main_loop < n_gen; main_loop++) {
    for (param = 0; param < n_dim*n_cov; param++) {
      /** Sample from the proposal distribution **/
      prop[param] = beta[param] + norm_rand()*sqrt(Var[param]);
      
      /** Calculating the ratio (log scale) **/
      /* prior */
      numer = dMVN(prop, beta0, A0, n_cov*n_dim, 1);
      denom = dMVN(beta, beta0, A0, n_cov*n_dim, 1);   
      /* likelihood */
      for (i = 0; i < n_samp; i++) {
	sumall = 1.0; sumall1 = 1.0;
	for (j = 0; j < n_dim; j++) {
	  dtemp = 0; dtemp1 = 0;
	  for (k = 0; k < n_cov; k++) {
	    dtemp += X[i][k]*beta[j*n_cov+k];
	    dtemp1 += X[i][k]*prop[j*n_cov+k];
	  }
	  if (Y[i] == (j+1)) {
	    denom += dtemp;
	    numer += dtemp1;
	  } 
	  sumall += exp(dtemp);
	  sumall1 += exp(dtemp1);
	}
	numer -= log(sumall1);
	denom -= log(sumall);
      }
      
      /** Rejection **/
      if (unif_rand() < fmin2(1.0, exp(numer-denom))) {
	counter[param]++;
	beta[param] = prop[param];
      }
    }
  }

  free(prop);
}

/* A Gibbs sampler for binary probit regression with and without
   marginal data augmentation 
*/ 

void bprobitGibbs(int *Y,        /* binary outcome variable */
		  double **X,    /* covariate matrix */
		  double *beta,  /* coefficients */
		  int n_samp,    /* # of obs */ 
		  int n_cov,     /* # of covariates */
		  int prior,     /* Should prior be included in X? */
		  double *beta0, /* prior mean */
		  double **A0,   /* prior precision */
		  int mda,       /* Want to use marginal data augmentation? */ 
		  int n_gen      /* # of gibbs draws */
		  ) {
  
  /*** model parameters ***/
  double **SS = doubleMatrix(n_cov+1, n_cov+1); /* matrix folders for SWEEP */
  double *mean = doubleArray(n_cov);            /* means for beta */
  double **V = doubleMatrix(n_cov, n_cov);      /* variances for beta */
  double *W = doubleArray(n_samp);
  double **mtemp = doubleMatrix(n_cov, n_cov);

  /*** storage parameters and loop counters **/
  int i, j, k, main_loop;  
  double dtemp;
  
  /*** marginal data augmentation ***/
  double sig2 = 1;
  int nu0 = 1;
  double s0 = 1;
  
  /*** read the prior as additional data points ***/
  if (prior) {
    dcholdc(A0, n_cov, mtemp);
    for(i = 0; i < n_cov; i++) {
      X[n_samp+i][n_cov] = 0;
      for(j = 0; j < n_cov; j++) {
	X[n_samp+i][n_cov] += mtemp[i][j]*beta0[j];
	X[n_samp+i][j] = mtemp[i][j];
      }
    }
  }

  /*** Gibbs Sampler! ***/
  for(main_loop = 1; main_loop <= n_gen; main_loop++){
    /* marginal data augmentation */
    if (mda) sig2 = s0/rchisq((double)nu0);
    
    for (i = 0; i < n_samp; i++){
      dtemp = 0;
      for (j = 0; j < n_cov; j++) 
	dtemp += X[i][j]*beta[j]; 
      if(Y[i] == 0) 
	W[i] = TruncNorm(dtemp-1000,0,dtemp,1,0);
      else 
	W[i] = TruncNorm(0,dtemp+1000,dtemp,1,0);
      X[i][n_cov] = W[i]*sqrt(sig2);
      W[i] *= sqrt(sig2);
    }

    /* SS matrix */
    for(j = 0; j <= n_cov; j++)
      for(k = 0; k <= n_cov; k++)
	SS[j][k]=0;
    for(i = 0;i < n_samp; i++)
      for(j = 0;j <= n_cov; j++)
	for(k = 0; k <= n_cov; k++) 
	  SS[j][k] += X[i][j]*X[i][k];
    for(i = n_samp;i < n_samp+n_cov; i++)
      for(j = 0;j <= n_cov; j++)
	for(k = 0; k <= n_cov; k++) 
	  SS[j][k] += X[i][j]*X[i][k];

    /* SWEEP SS matrix */
    for(j = 0; j < n_cov; j++)
      SWP(SS, j, n_cov+1);

    /* draw beta */    
    for(j = 0; j < n_cov; j++)
      mean[j] = SS[j][n_cov];
    if (mda) 
      sig2=(SS[n_cov][n_cov]+s0)/rchisq((double)n_samp+nu0);
    for(j = 0; j < n_cov; j++)
      for(k = 0; k < n_cov; k++) V[j][k]=-SS[j][k]*sig2;
    rMVN(beta, mean, V, n_cov);
 
    /* rescaling the parameters */
    if(mda) 
      for (j = 0; j < n_cov; j++) beta[j] /= sqrt(sig2);
    R_CheckUserInterrupt();
  } /* end of Gibbs sampler */

  /** freeing memory **/
  free(W);
  free(mean);
  FreeMatrix(SS, n_cov+1);
  FreeMatrix(V, n_cov);
  FreeMatrix(mtemp, n_cov);

} 

/* A routine used for Mixed Gaussian Model */
void compW(double **T, double sig2, double **X, int start, int size,
	   double *W_vec, int n, int p);
void compW(double **T, double sig2, double **X, int start, int size,
	   double *W_vec, int n, int p){

  int i,j,k;
  double **V = doubleMatrix(size, size);
  double **TEMP = doubleMatrix(size, p+1);
  double **W = doubleMatrix(size, size);     /* The weight matrix */
  double **W_inv = doubleMatrix(size, size); /* inverse of the weight matrix */
  
  for(i=0;i<size;i++){
    for(j=0;j<size;j++)V[i][j]=0;
    for(j=0;j<=p;j++)TEMP[i][j]=0;
  }
  for(i=0;i<size;i++)     /* row of X */
    for(j=0;j<=p;j++)   /* col of T */
      for(k=0;k<=p;k++)
	TEMP[i][j]+=T[k][j]*X[start+i][k];
  for(i=0;i<size;i++)     /* row of TEMP */
    for(j=0;j<size;j++)   /* col of t(X) */
      for(k=0;k<=p;k++)
	V[i][j]+=TEMP[i][k]*X[start+j][k];
  for(i=0;i<size;i++){
    for(j=0;j<size;j++) W_inv[i][j]=V[i][j];
    W_inv[i][i]+=sig2;
  }
  dinv(W_inv,size,W);

  for(j=0;j<size;j++)             /* Store W  */
    for(k=0;k<size;k++)
      W_vec[j*size+k]=W[j][k];

  FreeMatrix(V, size);
  FreeMatrix(TEMP, size);
  FreeMatrix(W, size);
  FreeMatrix(W_inv, size);
}

/* Multinomial Probit model from MNP: 
   It only works when */
