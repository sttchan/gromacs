/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * GRoups of Organic Molecules in ACtion for Science
 */
static char *SRCID_do_gct_c = "$Id$";

#include "typedefs.h"
#include "do_gct.h"
#include "block_tx.h"
#include "futil.h"
#include "xvgr.h"
#include "macros.h"
#include "physics.h"
#include "network.h"
#include "smalloc.h"
#include "string2.h"
#include "readinp.h"
#include "filenm.h"
#include "mdrun.h"
#include "update.h"
#include "vec.h"
#include "main.h"
#include "txtdump.h"

t_coupl_rec *init_coupling(FILE *log,int nfile,t_filenm fnm[],
			   t_commrec *cr,t_forcerec *fr,
			   t_mdatoms *md,t_idef *idef)
{
  int         i,nc,index,j;
  int         ati,atj;
  t_coupl_rec *tcr;
  
  snew(tcr,1);
  read_gct (opt2fn("-j",nfile,fnm), tcr);
  write_gct(opt2fn("-jo",nfile,fnm),tcr,idef);
  
  if ((tcr->dipole != 0.0) && (!ftp2bSet(efNDX,nfile,fnm))) {
    fatal_error(0,"Trying to use polarization correction to energy without specifying an index file for molecule numbers. This will generate huge induced dipoles, and hence not work!");
  }
  
  copy_ff(tcr,fr,md,idef);
    
  /* Update all processors with coupling info */
  if (PAR(cr))
    comm_tcr(log,cr,&tcr);
  
  return tcr;
}

real Ecouple(t_coupl_rec *tcr,real ener[])
{
  if (tcr->bInter)
    return ener[F_SR]+ener[F_LJ]+ener[F_LR]+ener[F_LJLR];
  else
    return ener[F_EPOT];
}

char *mk_gct_nm(char *fn,int ftp,int ati,int atj)
{
  static char buf[256];
  
  strcpy(buf,fn);
  if (atj == -1)
    sprintf(buf+strlen(fn)-4,"%d.%s",ati,ftp2ext(ftp));
  else
    sprintf(buf+strlen(fn)-4,"%d_%d.%s",ati,atj,ftp2ext(ftp));
  
  return buf;
}

static void pr_ff(t_coupl_rec *tcr,real time,t_idef *idef,
		  real ener[],real Virial,real mu,
		  t_commrec *cr,int nfile,t_filenm fnm[])
{
  static FILE *prop;
  static FILE **out=NULL;
  static FILE **qq=NULL;
  static FILE **ip=NULL;
  t_coupl_LJ  *tclj;
  t_coupl_BU  *tcbu;
  char        buf[256];
  char        *leg[] =  { "C12", "C6" };
  char        *bleg[] = { "A", "B", "C" };
  char        *pleg[] = { "RA-Pres", "Pres", "RA-Epot", "Epot", "Mu", "Ra-Mu" };
  char        *vleg[] = { "RA-Vir", "Vir", "RA-Epot", "Epot", "Mu", "Ra-Mu" };
  int         i,index;
  
  if ((prop == NULL) && (out == NULL) && (qq == NULL) && (ip == NULL)) {
    prop=xvgropen(opt2fn("-runav",nfile,fnm),
		  "Properties and Running Averages","Time (ps)","");
    if (tcr->bVirial)
      xvgr_legend(prop,asize(vleg),vleg);
    else
      xvgr_legend(prop,asize(pleg),pleg);
      
    if (tcr->nLJ) {
      snew(out,tcr->nLJ);
      for(i=0; (i<tcr->nLJ); i++) {
	if (tcr->tcLJ[i].bPrint) {
	  tclj   = &(tcr->tcLJ[i]);
	  out[i] = 
	    xvgropen(mk_gct_nm(opt2fn("-ffout",nfile,fnm),
			       efXVG,tclj->at_i,tclj->at_j),
		     "General Coupling Lennard Jones","Time (ps)",
		     "Force constant (units)");
	  fprintf(out[i],"@ subtitle \"Interaction between types %d and %d\"\n",
		  tclj->at_i,tclj->at_j);
	  xvgr_legend(out[i],asize(leg),leg);
	  fflush(out[i]);
	}
      }
    }
    else if (tcr->nBU) {
      snew(out,tcr->nBU);
      for(i=0; (i<tcr->nBU); i++) {
	if (tcr->tcBU[i].bPrint) {
	  tcbu=&(tcr->tcBU[i]);
	  out[i] = 
	    xvgropen(mk_gct_nm(opt2fn("-ffout",nfile,fnm),efXVG,
			       tcbu->at_i,tcbu->at_j),
		     "General Coupling Buckingham","Time (ps)",
		     "Force constant (units)");
	  fprintf(out[i],"@ subtitle \"Interaction between types %d and %d\"\n",
		  tcbu->at_i,tcbu->at_j);
	  xvgr_legend(out[i],asize(bleg),bleg);
	  fflush(out[i]);
	}
      }
    }
    snew(qq,tcr->nQ);
    for(i=0; (i<tcr->nQ); i++) {
      if (tcr->tcQ[i].bPrint) {
	qq[i] = xvgropen(mk_gct_nm(opt2fn("-ffout",nfile,fnm),efXVG,
				   tcr->tcQ[i].at_i,-1),
			 "General Coupling Charge","Time (ps)","Charge (e)");
	fprintf(qq[i],"@ subtitle \"Type %d\"\n",tcr->tcQ[i].at_i);
	fflush(qq[i]);
      }
    }
    snew(ip,tcr->nIP);
    for(i=0; (i<tcr->nIP); i++) {
      sprintf(buf,"gctIP%d",tcr->tIP[i].type);
      ip[i]=xvgropen(mk_gct_nm(opt2fn("-ffout",nfile,fnm),efXVG,0,-1),
		     "General Coupling iparams","Time (ps)","ip ()");
      index=tcr->tIP[i].type;
      fprintf(ip[i],"@ subtitle \"Coupling to %s\"\n",
	      interaction_function[idef->functype[index]].longname);
      fflush(ip[i]);
    }
  }
  if (tcr->bVirial)
    fprintf(prop,"%10g  %12.5e  %12.5e  %12.5e  %12.5e  %12.5e  %12.5e\n",
	    time,tcr->vir,Virial,tcr->epot,Ecouple(tcr,ener),
	    tcr->mu,mu);
  else
    fprintf(prop,"%10g  %12.5e  %12.5e  %12.5e  %12.5e  %12.5e  %12.5e\n",
	    time,tcr->pres,ener[F_PRES],tcr->epot,Ecouple(tcr,ener),
	    tcr->mu,mu);
  
  fflush(prop);
  for(i=0; (i<tcr->nLJ); i++) {
    tclj=&(tcr->tcLJ[i]);
    if (tclj->bPrint) {
      fprintf(out[i],"%14.7e  %14.7e  %14.7e\n",
	      time,tclj->c12,tclj->c6);
      fflush(out[i]);
    }
  }
  for(i=0; (i<tcr->nBU); i++) {
    tcbu=&(tcr->tcBU[i]);
    if (tcbu->bPrint) {
      fprintf(out[i],"%14.7e  %14.7e  %14.7e  %14.7e\n",
	      time,tcbu->a,tcbu->b,tcbu->c);
      fflush(out[i]);
    }
  }
  for(i=0; (i<tcr->nQ); i++) {
    if (tcr->tcQ[i].bPrint) {
      fprintf(qq[i],"%14.7e  %14.7e\n",time,tcr->tcQ[i].Q);
      fflush(qq[i]);
    }
  }
  for(i=0; (i<tcr->nIP); i++) {
    fprintf(ip[i],"%10g  ",time);
    index=tcr->tIP[i].type;
    switch(idef->functype[index]) {
    case F_BONDS:
      fprintf(ip[i],"%10g  %10g\n",tcr->tIP[i].iprint.harmonic.krA,
	      tcr->tIP[i].iprint.harmonic.rA);
      break;
    default:
      break;
    }
    fflush(ip[i]);
  }
}

static void pr_dev(bool bVirial,
		   real t,real dev[eoNR],t_commrec *cr,int nfile,t_filenm fnm[])
{
  static FILE *fp=NULL;
  int    i;
  
  if (!fp) {
    fp=xvgropen(opt2fn("-devout",nfile,fnm),
		"Deviations from target value",
		bVirial ? "Vir" : "Pres","Epot");
  }
  fprintf(fp,"%12.5e  %12.5e  %12.5e\n",t,bVirial ? dev[eoVir] : dev[eoPres],dev[eoEpot]);
  fflush(fp);
}

static void upd_nbfplj(FILE *log,real **nbfp,int atnr,real f6[],real f12[])
{
  int n,m,k;
  
  /* Update the nonbonded force parameters */
  for(k=n=0; (n<atnr); n++) {
    for(m=0; (m<atnr); m++,k++) {
      C6 (nbfp,n,m) *= f6[k];
      C12(nbfp,n,m) *= f12[k];
    }
  }
}

static void upd_nbfpbu(FILE *log,real **nbfp,int atnr,
			real fa[],real fb[],real fc[])
{
  int n,m,k;
  
  /* Update the nonbonded force parameters */
  for(k=n=0; (n<atnr); n++) {
    for(m=0; (m<atnr); m++) {
      (nbfp)[n][3*m]   *= fa[k];
      (nbfp)[n][3*m+1] *= fb[k];
      (nbfp)[n][3*m+2] *= fc[k];
    }
  }
}

void gprod(t_commrec *cr,int n,real f[])
{
  /* Compute the global product of all elements in an array 
   * such that after gprod f[i] = PROD_j=1,nprocs f[i][j]
   */
  int  i,j;
  real *buf;
  
  snew(buf,n);
  
  for(i=0; (i<=cr->nprocs); i++) {
    gmx_tx(cr->left,array(f,n));
    gmx_rx(cr->right,array(buf,n));
    gmx_wait(cr->left,cr->right);
    for(j=0; (j<n); j++)
      f[j] *= buf[j];
  }
  sfree(buf);
}

void set_factor_matrix(int ntypes,real f[],real fmult,int ati,int atj)
{
#define FMIN 0.95
#define FMAX 1.05
  int i;

  fprintf(stdlog,"fmult before: %g",fmult);
  fmult = min(FMAX,max(FMIN,fmult));  
  fprintf(stdlog,", after: %g\n",fmult);
  if (atj != -1) {
    f[ntypes*ati+atj] *= fmult;
    f[ntypes*atj+ati] *= fmult;
  }
  else {
    for(i=0; (i<ntypes); i++) {
      f[ntypes*ati+i] *= fmult;
      f[ntypes*i+ati] *= fmult;
    }
  }
#undef FMIN
#undef FMAX
}

real calc_deviation(real xav,real xt,real x0)
{
  real dev;
  
  /* This may prevent overshooting in GCT coupling... */
  if (xav > x0) {
    if (xt > x0)
      dev = /*max(x0-xav,x0-xt);*/ min(xav-x0,xt-x0);
    else
      dev = 0;
  }
  else {
    if (xt < x0)
      dev = max(xav-x0,xt-x0);
    else
      dev = 0;
  }
  return x0-xav;
}

static real calc_dist(FILE *log,rvec x[])
{
  static bool bFirst=TRUE;
  static bool bDist;
  static int i1,i2;
  char *buf;
  rvec   dx;
  
  if (bFirst) {
    if ((buf = getenv("DISTGCT")) == NULL)
      bDist = FALSE;
    else {
      bDist  = (sscanf(buf,"%d%d",&i1,&i2) == 2);
      if (bDist)
	fprintf(log,"Will couple to distance between %d and %d\n",i1,i2);
      else
	fprintf(log,"Will not couple to distances\n");
    }
    bFirst = FALSE;
  }
  if (bDist) {
    rvec_sub(x[i1],x[i2],dx);
    return norm(dx);
  }
  else
    return 0.0;
}

void calc_force(int natom,rvec f[])
{
  int  i,j,m;
  int  jindex[] = { 0, 4, 8};
  rvec fff[2],xxx[2],dx,df;
  real msf1,msf2;
  
  for(j=0; (j<2); j++) {
    clear_rvec(fff[j]);
    for(i=jindex[j]; (i<jindex[j+1]); i++) {
      for(m=0; (m<DIM); m++) {
	fff[j][m] += f[i][m];
      }
    }
  }
  
  msf1 = iprod(fff[0],fff[0]);
  msf2 = iprod(fff[1],fff[1]);

  pr_rvecs(stdlog,0,"force",f,natom);
    
  fprintf(stdlog,"FMOL:  %10.3f  %10.3f  %10.3f  %10.3f  %10.3f  %10.3f\n",
	  fff[0][XX],fff[0][YY],fff[0][ZZ],fff[1][XX],fff[1][YY],fff[1][ZZ]);
  fprintf(stdlog,"RMSF:  %10.3e  %10.3e\n",msf1,msf2);
  
}

void do_coupling(FILE *log,int nfile,t_filenm fnm[],
		 t_coupl_rec *tcr,real t,int step,real ener[],
		 t_forcerec *fr,t_inputrec *ir,bool bMaster,
		 t_mdatoms *md,t_idef *idef,real mu_aver,int nmols,
		 t_commrec *cr,matrix box,tensor virial,rvec mu_tot,
		 rvec x[],rvec f[])
{

#define enm2Debye 48.0321
#define d2e(x) (x)/enm2Debye
#define enm2kjmol(x) (x)*0.0143952 /* = 2.0*4.0*M_PI*EPSILON0 */

  static real *f6,*f12,*fa,*fb,*fc,*fq;
  static bool bFirst = TRUE;
  
  int         i,j,ati,atj,atnr2,type,ftype;
  real        deviation[eoNR],prdev[eoNR],epot0,dist,rmsf;
  real        ff6,ff12,ffa,ffb,ffc,ffq,factor,dt,mu_ind;
  real        Epol,Eintern,Virial,muabs,xiH,xiS,xi6,xi12;
  bool        bTest,bPrint;
  t_coupl_LJ  *tclj;
  t_coupl_BU  *tcbu;
  t_coupl_Q   *tcq;
  t_coupl_iparams *tip;
  
  atnr2 = idef->atnr * idef->atnr;
  if (bFirst) {
    if (PAR(cr))
      fprintf(log,"DOGCT: this is parallel\n");
    else
      fprintf(log,"DOGCT: this is not parallel\n");
    snew(f6, atnr2);
    snew(f12,atnr2);
    snew(fa, atnr2);
    snew(fb, atnr2);
    snew(fc, atnr2);
    snew(fq, idef->atnr);
    bFirst = FALSE;
    
    if (tcr->bVirial) {
      int  nrdf = 0;
      real TTT  = 0;
      real Vol  = det(box);
      
      for(i=0; (i<ir->opts.ngtc); i++) {
	nrdf += ir->opts.nrdf[i];
	TTT  += ir->opts.nrdf[i]*ir->opts.ref_t[i];
      }
      TTT /= nrdf;
      
      /* Calculate reference virial from reference temperature and pressure */
      tcr->vir0 = 0.5*BOLTZ*nrdf*TTT - (3.0/2.0)*Vol*tcr->pres0;
      
      fprintf(log,"DOGCT: TTT = %g, nrdf = %d, vir0 = %g,  Vol = %g\n",
	      TTT,nrdf,tcr->vir0,Vol);
    }
  }
  
  bPrint = do_per_step(step,ir->nstprint);
  dt     = ir->delta_t;

  /* Initiate coupling to the reference pressure and temperature to start
   * coupling slowly.
   */
  if (step == 0) {
    tcr->pres = tcr->pres0;
    tcr->vir  = tcr->vir0;
    tcr->dist = tcr->dist0;
    tcr->force= tcr->force0;
    tcr->mu   = tcr->mu0;
    if ((tcr->dipole) != 0.0) {
      mu_ind = mu_aver - d2e(tcr->dipole); /* in e nm */
      Epol = mu_ind*mu_ind/(enm2kjmol(tcr->polarizability));
      tcr->epot = (tcr->epot0-Epol)*nmols;
    }
    else
      tcr->epot = tcr->epot0*nmols;
  }

  /* We want to optimize the LJ params, usually to the Vaporization energy 
   * therefore we only count intermolecular degrees of freedom.
   * Note that this is now optional. switch UseEinter to yes in your gct file
   * if you want this.
   */
  dist      = calc_dist(log,x);
  muabs     = norm(mu_tot);
  Eintern   = Ecouple(tcr,ener);
  Virial    = virial[XX][XX]+virial[YY][YY]+virial[ZZ][ZZ];

  if (bPrint)
    calc_force(md->nr,f);
  
  /* Use a memory of tcr->nmemory steps, so we actually couple to the
   * average observable over the last tcr->nmemory steps. This may help
   * in avoiding local minima in parameter space.
   */
  tcr->pres = run_aver(tcr->pres,ener[F_PRES],step,tcr->nmemory);
  tcr->epot = run_aver(tcr->epot,Eintern,     step,tcr->nmemory);
  tcr->vir  = run_aver(tcr->vir, Virial,      step,tcr->nmemory);
  tcr->mu   = run_aver(tcr->mu,muabs,step,tcr->nmemory);
  tcr->dist = run_aver(tcr->dist,dist,step,tcr->nmemory);
  
  if (bPrint)
    pr_ff(tcr,t,idef,ener,Virial,muabs,cr,nfile,fnm);

  /* If dipole != 0.0 assume we want to use polarization corrected coupling */
  if ((tcr->dipole) != 0.0) {
    mu_ind = mu_aver - d2e(tcr->dipole); /* in e nm */
    
    Epol = mu_ind*mu_ind/(enm2kjmol(tcr->polarizability));

    epot0 = (tcr->epot0 - Epol)*nmols;
    if (debug) {
      fprintf(debug,"mu_ind: %g (%g D) mu_aver: %g (%g D)\n",
	      mu_ind,mu_ind*enm2Debye,mu_aver,mu_aver*enm2Debye);
      fprintf(debug,"Eref %g Epol %g Erunav %g Eact %g\n",
	      (tcr->epot0)*nmols, Epol*nmols, tcr->epot,ener[F_EPOT]);
    }
  }
  else 
    epot0 = tcr->epot0*nmols;

  /* Calculate the deviation of average value from the target value */
  deviation[eoPres]   = calc_deviation(tcr->pres, ener[F_PRES],tcr->pres0);
  deviation[eoEpot]   = calc_deviation(tcr->epot, Eintern,     epot0);
  deviation[eoVir]    = calc_deviation(tcr->vir,  Virial,      tcr->vir0);
  deviation[eoDist]   = calc_deviation(tcr->dist, dist,        tcr->dist0);
  deviation[eoMu]     = calc_deviation(tcr->mu,   muabs,       tcr->mu0);
  calc_f_dev(md->nr,md->chargeA,x,idef,&xiH,&xiS);
  
  prdev[eoPres]   = tcr->pres0 - ener[F_PRES];
  prdev[eoEpot]   = epot0      - Eintern;
  prdev[eoVir]    = tcr->vir0  - Virial;
  prdev[eoMu]     = tcr->mu0   - muabs;
  prdev[eoDist]   = tcr->dist0 - dist;
  prdev[eoForce]  = tcr->force0- rmsf;
  
  if (bPrint)
    pr_dev(tcr->bVirial,t,prdev,cr,nfile,fnm);
  
  /* First set all factors to 1 */
  for(i=0; (i<atnr2); i++) {
    f6[i] = f12[i] = fa[i] = fb[i] = fc[i] = 1.0;
  }
  for(i=0; (i<idef->atnr); i++) 
    fq[i] = 1.0;
  
  if (!fr->bBHAM) {
    for(i=0; (i<tcr->nLJ); i++) {
      tclj=&(tcr->tcLJ[i]);

      ati=tclj->at_i;
      atj=tclj->at_j;
      
      ff6 = ff12 = 1.0;	
      
      if (tclj->eObs == eoForce) {
	fprintf(log,"Have computed derivatives: xiH = %g, xiS = %g\n",xiH,xiS);
	if (ati == 1) {
	  /* Hydrogen */
	  ff12 += max(-1,dt*xiH); 
	}
	else if (ati == 2) {
	  /* Shell */
	  ff12 += max(-1,dt*xiS); 
	}
	else
	  fatal_error(0,"No H, no Shell, edit code at %s, line %d\n",__FILE__,__LINE__);
      }
      else {
	fprintf(log,"tcr->tcLJ[%d].xi_12 = %g deviation = %g\n",i,
		tclj->xi_12,deviation[tclj->eObs]);
	factor=deviation[tclj->eObs];
	
	xi6  = tclj->xi_6;
	xi12 = tclj->xi_12;
	
	if (xi6)      
	  ff6  += (dt/xi6)  * factor;
	if (xi12)     
	  ff12 += (dt/xi12) * factor;
      }

      set_factor_matrix(idef->atnr,f6, sqrt(ff6), ati,atj);
      set_factor_matrix(idef->atnr,f12,sqrt(ff12),ati,atj);
    }
    if (PAR(cr)) {
      gprod(cr,atnr2,f6);
      gprod(cr,atnr2,f12);
    }
    upd_nbfplj(log,fr->nbfp,idef->atnr,f6,f12);
    
    /* Copy for printing */
    for(i=0; (i<tcr->nLJ); i++) {
      tclj=&(tcr->tcLJ[i]);
      tclj->c6  =  C6(fr->nbfp,tclj->at_i,tclj->at_i);
      tclj->c12 = C12(fr->nbfp,tclj->at_i,tclj->at_i);
    }
  }
  else {
    for(i=0; (i<tcr->nBU); i++) {
      tcbu=&(tcr->tcBU[i]);
      
      factor=deviation[tcbu->eObs];
      
      if (tcbu->xi_a)      
	ffa = 1 + (dt/tcbu->xi_a)  * factor;
      if (tcbu->xi_b)      
	  ffb = 1 + (dt/tcbu->xi_b)  * factor;
      if (tcbu->xi_c)      
	ffc = 1 + (dt/tcbu->xi_c)  * factor;
      
      ati=tcbu->at_i;
      atj=tcbu->at_j;
      set_factor_matrix(idef->atnr,fa,sqrt(ffa),ati,atj);
      set_factor_matrix(idef->atnr,fa,sqrt(ffb),ati,atj);
      set_factor_matrix(idef->atnr,fc,sqrt(ffc),ati,atj);
    }
    if (PAR(cr)) {
      gprod(cr,atnr2,fa);
      gprod(cr,atnr2,fb);
      gprod(cr,atnr2,fc);
    }
    upd_nbfpbu(log,fr->nbfp,idef->atnr,fa,fb,fc);
  }
  
  for(i=0; (i<tcr->nQ); i++) {
    tcq=&(tcr->tcQ[i]);
    fprintf(log,"tcr->tcQ[%d].xi_Q = %g deviation = %g\n",i,
	    tcq->xi_Q,deviation[tcq->eObs]);
    if (tcq->xi_Q)     
      ffq = 1.0 + (dt/tcq->xi_Q) * deviation[tcq->eObs];
    else
      ffq = 1.0;
    fq[tcq->at_i] *= ffq;
    
  }
  if (PAR(cr))
    gprod(cr,idef->atnr,fq);
  
  for(j=0; (j<md->nr); j++) {
    md->chargeA[j] *= fq[md->typeA[j]];
  }
  for(i=0; (i<tcr->nQ); i++) {
    tcq=&(tcr->tcQ[i]);
    for(j=0; (j<md->nr); j++) {
      if (md->typeA[j] == tcq->at_i) {
	tcq->Q = md->chargeA[j];
	break;
      }
    }
    if (j == md->nr)
      fatal_error(0,"Coupling type %d not found",tcq->at_i);
  }  
  for(i=0; (i<tcr->nIP); i++) {
    tip    = &(tcr->tIP[i]);
    type   = tip->type;
    ftype  = idef->functype[type];
    factor = dt*deviation[tip->eObs];
    
    /* Time for a killer macro */
#define DOIP(ip) if (tip->xi.##ip) idef->iparams[type].##ip *= (1+factor/tip->xi.##ip)
      
    switch(ftype) {
    case F_BONDS:
      DOIP(harmonic.krA);
      DOIP(harmonic.rA);
	break;
    default:
      break;
    }
#undef DOIP
    tip->iprint=idef->iparams[type];
  }
}

