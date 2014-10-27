#include<iostream>
#include<sstream>
#include<string>
#include<fstream>
#include<cmath>
#include<iomanip>

#include<gsl/gsl_sf_bessel.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_rng.h>            // gsl random number generators
#include <gsl/gsl_randist.h>        // gsl random number distributions
#include <gsl/gsl_vector.h>         // gsl vector and matrix definitions
#include <gsl/gsl_blas.h>           // gsl linear algebra stuff
#include <gsl/gsl_multifit_nlin.h>  // gsl multidimensional fitting

#include "HBT.h"
#include "arsenal.h"
#include "ParameterReader.h"

using namespace std;

HBT::HBT(string path_in, ParameterReader* paraRdr_in, particle_info* particle_in, int particle_idx, FO_surf* FOsurf_ptr_in, int FOarray_length)
{
   path = path_in;
   paraRdr =  paraRdr_in;
   particle_ptr = particle_in;
   particle_id = particle_idx;

   FOsurf_ptr = FOsurf_ptr_in;
   FO_length = FOarray_length;

   // initialize eta_s array
   eta_s_npts = paraRdr->getVal("eta_s_npts");
   double eta_s_f = paraRdr->getVal("eta_s_f");
   eta_s = new double [eta_s_npts];
   eta_s_weight = new double [eta_s_npts];
   gauss_quadrature(eta_s_npts, 1, 0.0, 0.0, 0.0, eta_s_f, eta_s, eta_s_weight);

   // initialize emission function
   Emissionfunction_length = FO_length*eta_s_npts;
   Emissionfunction_Data = new double [Emissionfunction_length];
   Emissionfunction_t = new double [Emissionfunction_length];
   Emissionfunction_x = new double [Emissionfunction_length];
   Emissionfunction_y = new double [Emissionfunction_length];
   Emissionfunction_z = new double [Emissionfunction_length];
   Emissionfunction_Data_CDF = new double [Emissionfunction_length];
   for(int i=0; i<Emissionfunction_length; i++)
   {
      Emissionfunction_Data[i] = 0.0;
      Emissionfunction_t[i] = 0.0;
      Emissionfunction_x[i] = 0.0;
      Emissionfunction_y[i] = 0.0;
      Emissionfunction_z[i] = 0.0;
      Emissionfunction_Data_CDF[i] = 0.0;
   }

   INCLUDE_SHEAR_DELTAF = paraRdr->getVal("turn_on_shear");
   INCLUDE_BULK_DELTAF = paraRdr->getVal("turn_on_bulk");
   flag_neg = paraRdr->getVal("flag_neg");

   // initialize correlation function
   qnpts = paraRdr->getVal("qnpts");
   MCint_calls = paraRdr->getVal("MCint_calls");
   fit_tolarence = paraRdr->getVal("fit_tolarence");
   fit_max_iterations = paraRdr->getVal("fit_max_iterations");

   double init_q = paraRdr->getVal("init_q");
   double delta_q = paraRdr->getVal("delta_q");
   q_out = new double [qnpts];
   q_side = new double [qnpts];
   q_long = new double [qnpts];
   for(int i=0; i<qnpts; i++)
   {
      q_out[i] = init_q + (double)i * delta_q;
      q_side[i] = init_q + (double)i * delta_q;
      q_long[i] = init_q + (double)i * delta_q;
   }

   Correl_1D_out = new double [qnpts];
   Correl_1D_side = new double [qnpts];
   Correl_1D_long = new double [qnpts];
   Correl_1D_out_err = new double [qnpts];
   Correl_1D_side_err = new double [qnpts];
   Correl_1D_long_err = new double [qnpts];
   for(int i=0; i<qnpts; i++)
   {
      Correl_1D_out[i] = 0.0;
      Correl_1D_side[i] = 0.0;
      Correl_1D_long[i] = 0.0;
      Correl_1D_out_err[i] = 0.0;
      Correl_1D_side_err[i] = 0.0;
      Correl_1D_long_err[i] = 0.0;
   }
   
   Correl_3D = new double** [qnpts];
   Correl_3D_err = new double** [qnpts];
   for(int i=0; i<qnpts; i++)
   {
      Correl_3D[i] = new double* [qnpts];
      Correl_3D_err[i] = new double* [qnpts];
      for(int j=0; j<qnpts; j++)
         Correl_3D[i][j] = new double [qnpts];
      for(int j=0; j<qnpts; j++)
         Correl_3D_err[i][j] = new double [qnpts];
   }
   for(int i=0; i<qnpts; i++)
      for(int j=0; j<qnpts; j++)
         for(int k=0; k<qnpts; k++)
         {
            Correl_3D[i][j][k] = 0.0;
            Correl_3D_err[i][j][k] = 0.0;
         }

   return;
}

HBT::~HBT()
{

   delete[] Emissionfunction_Data;
   delete[] Emissionfunction_t;
   delete[] Emissionfunction_x;
   delete[] Emissionfunction_y;
   delete[] Emissionfunction_z;
   delete[] Emissionfunction_Data_CDF;

   delete[] q_out;
   delete[] q_side;
   delete[] q_long;

   delete[] Correl_1D_out;
   delete[] Correl_1D_side;
   delete[] Correl_1D_long;
   delete[] Correl_1D_out_err;
   delete[] Correl_1D_side_err;
   delete[] Correl_1D_long_err;

   for(int i=0; i<qnpts; i++)
   {
      for(int j=0; j< qnpts; j++)
          delete[] Correl_3D[i][j];
      delete[] Correl_3D[i];
      for(int j=0; j< qnpts; j++)
          delete[] Correl_3D_err[i][j];
      delete[] Correl_3D_err[i];
   }
   delete[] Correl_3D;
   delete[] Correl_3D_err;

   return;
}

void HBT::calculate_azimuthal_dependent_HBT_radii(double p_T, double p_phi, double y)
{
   cout << "Calculating "<< particle_ptr[particle_id].name << endl;

   K_T = p_T;
   K_phi = p_phi;
   K_y = y;
   
   SetEmissionData(FOsurf_ptr, y, p_T, p_phi);
   Cal_HBTRadii_fromEmissionfunction();
   //HBT_hadron.Cal_correlationfunction_1D_MC();
   //HBT_hadron.Fit_Correlationfunction1D();
   //HBT_hadron.Output_Correlationfunction_1D();
   //HBT_hadron.Cal_correlationfunction_3D_MC();
   //HBT_hadron.Fit_Correlationfunction3D();
}

void HBT::SetEmissionData(FO_surf* FO_surface, double K_rap, double K_T, double K_phi)
// compute emission function at a given pair momentum
{
  double tol = 1e-15;
  double mass = particle_ptr[particle_id].mass;
  double mT = sqrt(mass*mass + K_T*K_T);
  double K_x = K_T*cos(K_phi);
  double K_y = K_T*sin(K_phi);

  int idx = 0;
  double CDF = 0.0;
  for(int i=0; i<eta_s_npts; i++)
  {
      double local_eta_s = eta_s[i];
      double ch_localetas = cosh(local_eta_s);
      double sh_localetas = sinh(local_eta_s);

      double K_0 = mT*cosh(K_rap - local_eta_s);
      double K_z = mT*sinh(K_rap - local_eta_s);

      for (int j = 0; j < FO_length; j++)
	{
	  double S_p = 0.0e0;
        S_p = Emissionfunction(K_0, K_x, K_y, K_z, &FO_surface[j]);
        if (flag_neg == 1 && S_p < tol)
        {
           S_p = 0.0e0;
        }
	  else
        {
           double S_p_withweight = S_p*FO_surface[j].tau*eta_s_weight[i];
           Emissionfunction_Data[idx] = S_p_withweight; 
           Emissionfunction_t[idx] = FO_surface[j].tau*ch_localetas;
           Emissionfunction_x[idx] = FO_surface[j].xpt;
           Emissionfunction_y[idx] = FO_surface[j].ypt;
           Emissionfunction_z[idx] = FO_surface[j].tau*sh_localetas;
           CDF += S_p_withweight;
           Emissionfunction_Data_CDF[idx] = CDF;
           idx++;
        }
      }
  }
  Emissionfunction_length = idx;

  //normalize CDF to unity
  for(int i=0; i<Emissionfunction_length; i++)
  {
     Emissionfunction_Data_CDF[i] = Emissionfunction_Data_CDF[i] / CDF;
  }
  return;
}

void HBT::SetEmissionData(FO_surf* FO_surface, double K_rap, double K_T)
// compute the azimuthal averaged emission function at a given pair momentum
{
  double tol = 1e-15;
  double mass = particle_ptr[particle_id].mass;
  double mT = sqrt(mass*mass + K_T*K_T);

  int n_Kphi = paraRdr->getVal("n_Kphi");
  double *Kphi = new double [n_Kphi];
  double *Kphi_weight = new double [n_Kphi];
  gauss_quadrature(n_Kphi, 1, 0.0, 0.0, 0.0, 2*M_PI, Kphi, Kphi_weight);

  double *K_x = new double [n_Kphi];
  double *K_y = new double [n_Kphi];
  for(int i = 0; i < n_Kphi; i++)
  {
      K_x[i] = K_T*cos(Kphi[i]);
      K_y[i] = K_T*sin(Kphi[i]);
  }

  int idx = 0;
  double CDF = 0.0;
  for(int i = 0; i < eta_s_npts; i++)
  {
      double local_eta_s = eta_s[i];
      double ch_localetas = cosh(local_eta_s);
      double sh_localetas = sinh(local_eta_s);

      double K_0 = mT*cosh(K_rap - local_eta_s);
      double K_z = mT*sinh(K_rap - local_eta_s);

      for (int j = 0; j < FO_length; j++)
	{
	  double S_p = 0.0e0;
        for(int k = 0; k < n_Kphi; k++)
        {
           S_p += Emissionfunction(K_0, K_x[k], K_y[k], K_z, &FO_surface[j])*Kphi_weight[k];
        }
        if (flag_neg == 1 && S_p < tol)
        {
           S_p = 0.0e0;
        }
	  else
        {
           double S_p_withweight = S_p*FO_surface[j].tau*eta_s_weight[i];
           Emissionfunction_Data[idx] = S_p_withweight; 
           Emissionfunction_t[idx] = FO_surface[j].tau*ch_localetas;
           Emissionfunction_x[idx] = FO_surface[j].xpt;
           Emissionfunction_y[idx] = FO_surface[j].ypt;
           Emissionfunction_z[idx] = FO_surface[j].tau*sh_localetas;
           CDF += S_p_withweight;
           Emissionfunction_Data_CDF[idx] = CDF;
           idx++;
        }
      }
  }
  Emissionfunction_length = idx;

  //normalize CDF to unity
  for(int i=0; i<Emissionfunction_length; i++)
  {
     Emissionfunction_Data_CDF[i] = Emissionfunction_Data_CDF[i] / CDF;
  }

  delete [] Kphi;
  delete [] Kphi_weight;
  delete [] K_x;
  delete [] K_y;

  return;
}

void HBT::SetEmissionData(FO_surf* FO_surface, double K_rap)
// compute the azimuthal averaged K_T-integrated emission function
{
  double tol = 1e-15;
  double mass = particle_ptr[particle_id].mass;

  int n_KT = paraRdr->getVal("n_KT");
  double KT_min = paraRdr->getVal("KT_min");
  double KT_max = paraRdr->getVal("KT_max");
  double *KT_local = new double [n_KT];
  double *KT_local_weight = new double [n_KT];
  gauss_quadrature(n_KT, 1, 0.0, 0.0, KT_min, KT_max, KT_local, KT_local_weight);

  int n_Kphi = paraRdr->getVal("n_Kphi");
  double *Kphi = new double [n_Kphi];
  double *Kphi_weight = new double [n_Kphi];
  gauss_quadrature(n_Kphi, 1, 0.0, 0.0, 0.0, 2*M_PI, Kphi, Kphi_weight);

  double *mT = new double [n_KT];
  for(int i = 0; i < n_KT; i++)
     mT[i] = sqrt(mass*mass + KT_local[i]*KT_local[i]);

  double **K_x = new double* [n_KT];
  double **K_y = new double* [n_KT];
  for(int i = 0; i < n_KT; i++)
  {
      K_x[i] = new double [n_Kphi];
      K_y[i] = new double [n_Kphi];
      for(int j = 0; j < n_Kphi; j++)
      {
          K_x[i][j] = KT_local[i]*cos(Kphi[j]);
          K_y[i][j] = KT_local[i]*sin(Kphi[j]);
      }
  }

  int idx = 0;
  double CDF = 0.0;
  for(int i = 0; i < eta_s_npts; i++)
  {
      double local_eta_s = eta_s[i];
      double ch_localetas = cosh(local_eta_s);
      double sh_localetas = sinh(local_eta_s);

      double cosh_y_minus_etas = cosh(K_rap - local_eta_s);
      double sinh_y_minus_etas = sinh(K_rap - local_eta_s);

      for (int j = 0; j < FO_length; j++)
	{
	  double S_p = 0.0e0;
        for(int k = 0; k < n_KT; k++)
        {
           double K_0 = mT[k]*cosh_y_minus_etas;
           double K_z = mT[k]*sinh_y_minus_etas;
           double temp_dNpTdpTdphi = 0.0e0;
           for(int l = 0; l < n_Kphi; l++)
              temp_dNpTdpTdphi += Emissionfunction(K_0, K_x[k][l], K_y[k][l], K_z, &FO_surface[j])*Kphi_weight[l];
           S_p += temp_dNpTdpTdphi*KT_local[k]*KT_local_weight[k];
        }
        if (flag_neg == 1 && S_p < tol)
        {
           S_p = 0.0e0;
        }
	  else
        {
           double S_p_withweight = S_p*FO_surface[j].tau*eta_s_weight[i];
           Emissionfunction_Data[idx] = S_p_withweight; 
           Emissionfunction_t[idx] = FO_surface[j].tau*ch_localetas;
           Emissionfunction_x[idx] = FO_surface[j].xpt;
           Emissionfunction_y[idx] = FO_surface[j].ypt;
           Emissionfunction_z[idx] = FO_surface[j].tau*sh_localetas;
           CDF += S_p_withweight;
           Emissionfunction_Data_CDF[idx] = CDF;
           idx++;
        }
      }
  }
  Emissionfunction_length = idx;

  //normalize CDF to unity
  for(int i=0; i<Emissionfunction_length; i++)
  {
     Emissionfunction_Data_CDF[i] = Emissionfunction_Data_CDF[i] / CDF;
  }

  delete [] KT_local;
  delete [] KT_local_weight;
  delete [] Kphi;
  delete [] Kphi_weight;
  delete [] mT;
  for(int i = 0; i < n_KT; i++)
  {
      delete [] K_x[i];
      delete [] K_y[i];
  }
  delete [] K_x;
  delete [] K_y;

  return;
}

double HBT::Emissionfunction(double p0, double px, double py, double pz, FO_surf* surf)
{
   double mu = surf->particle_mu[particle_id];
   double sign = particle_ptr[particle_id].sign;
   double degen = particle_ptr[particle_id].gspin;

   double gammaT = surf->u0;
   double vx = surf->u1/gammaT;
   double vy = surf->u2/gammaT;
   double Tdec = surf->Tdec;
   double Pdec = surf->Pdec;
   double Edec = surf->Edec;
   double da0 = surf->da0;
   double da1 = surf->da1;
   double da2 = surf->da2;
   double pi00 = surf->pi00;
   double pi01 = surf->pi01;
   double pi02 = surf->pi02;
   double pi11 = surf->pi11;
   double pi12 = surf->pi12;
   double pi22 = surf->pi22;
   double pi33 = surf->pi33;

   double expon = (gammaT*(p0*1. - px*vx - py*vy) - mu) / Tdec;
   double f0 = 1./(exp(expon)+sign);       //thermal equilibrium distributions

   //p^mu d^3sigma_mu: The plus sign is due to the fact that the DA# variables are for the covariant surface integration
   double pdsigma = p0*da0 + px*da1 + py*da2;

   //viscous corrections
   double Wfactor = p0*p0*pi00 - 2.0*p0*px*pi01 - 2.0*p0*py*pi02 + px*px*pi11 + 2.0*px*py*pi12 + py*py*pi22 + pz*pz*pi33;
   double deltaf = INCLUDE_SHEAR_DELTAF*(1. - sign*f0)*Wfactor/(2.0*Tdec*Tdec*(Edec+Pdec));

   double dN_dyd2pTdphi;
   if (deltaf < -1.0)  // delta f correction is too large
      dN_dyd2pTdphi = 0.0;
   else
      dN_dyd2pTdphi = 1.0*degen/(8.0*(M_PI*M_PI*M_PI))*pdsigma*f0*(1+deltaf);
   //out << "Spectral funct = " << dN_dyd2pTdphi << endl;

   return (dN_dyd2pTdphi);
}

void HBT::Cal_HBTRadii_fromEmissionfunction()
{
  double* resultsX = new double[15];
  for(int i = 0; i < 15; i++)
     resultsX[i] = 0.0e0;

  for(int i=0; i < Emissionfunction_length; i++)
  {
     double S_p = Emissionfunction_Data[i];
     double tpt = Emissionfunction_t[i];
     double xpt = Emissionfunction_x[i];
     double ypt = Emissionfunction_y[i];
     double zpt = Emissionfunction_z[i];
 
     for(int ii=0; ii<2; ii++) // assuming reflection symmetry in eta_s
     {
        zpt = zpt*(-1);
        resultsX[0]  += S_p;             //single particle spectra <1>
        resultsX[1]  += S_p*xpt;         //<x>
        resultsX[2]  += S_p*ypt;         //<y>
        resultsX[3]  += S_p*zpt;         //<z>
        resultsX[4]  += S_p*xpt*ypt;     //<xy>
        resultsX[5]  += S_p*xpt*xpt;     //<xx>
        resultsX[6]  += S_p*ypt*ypt;     //<yy>
        resultsX[7]  += S_p*tpt;         //<t>
        resultsX[8]  += S_p*tpt*xpt;     //<tx>
        resultsX[9]  += S_p*tpt*ypt;     //<ty>
        resultsX[10] += S_p*tpt*zpt;     //<tz>
        resultsX[11] += S_p*zpt*zpt;     //<zz>
        resultsX[12] += S_p*tpt*tpt;     //<tt>
        resultsX[13] += S_p*xpt*zpt;     //<xz>
        resultsX[14] += S_p*ypt*zpt;     //<yz>
     }
  }
                                          	
  for(int i=0; i<15; i++)     //change to correct unit
     resultsX[i] = resultsX[i]/hbarC/hbarC/hbarC;
  
  spectra = resultsX[0];
  double meanx  = resultsX[1];
  double meany  = resultsX[2];
  double meanz  = resultsX[3];
  double meanxy = resultsX[4];
  double meanxx = resultsX[5];
  double meanyy = resultsX[6];
  double meant  = resultsX[7];
  double meanxt = resultsX[8];
  double meanyt = resultsX[9];
  double meanzt = resultsX[10];
  double meanzz = resultsX[11];
  double meantt = resultsX[12];
  double meanxz = resultsX[13];
  double meanyz = resultsX[14];
  
  //calculate the components of S^{\mu\nu} tensor
  double S00 = meantt/spectra - meant/spectra * meant/spectra;
  double S01 = meanxt/spectra - meanx/spectra * meant/spectra;
  double S02 = meanyt/spectra - meany/spectra * meant/spectra;
  double S03 = meanzt/spectra - meanz/spectra * meant/spectra;
  double S11 = meanxx/spectra - meanx/spectra * meanx/spectra;
  double S12 = meanxy/spectra - meanx/spectra * meany/spectra;
  double S13 = meanxz/spectra - meanx/spectra * meanz/spectra;
  double S22 = meanyy/spectra - meany/spectra * meany/spectra;
  double S23 = meanyz/spectra - meany/spectra * meanz/spectra;
  double S33 = meanzz/spectra - meanz/spectra * meanz/spectra;
  
  //calculate HBT radii from single particle emission function
  double mass = particle_ptr[particle_id].mass;
  double m_T = sqrt(mass*mass + K_T*K_T);
  double beta_T = K_T/m_T;
  double beta_L = 0.5*log((1+K_y)/(1-K_y));

  double R_out2 = S11*cos(K_phi)*cos(K_phi) + S22*sin(K_phi)*sin(K_phi) 
                  + S12*sin(2*K_phi) - 2*beta_T*(S01*cos(K_phi) 
                  + S02*sin(K_phi))+ beta_T*beta_T*S00;   //R_out^2
  double R_side2 = S11*sin(K_phi)*sin(K_phi) + S22*cos(K_phi)*cos(K_phi) 
                   - S12*sin(2*K_phi);          //R_side^2
  double R_long2 = S33 - 2*beta_L*S03 + beta_L*beta_L*S00;  //R_long^2

  R_out_EM = sqrt(R_out2);
  R_side_EM = sqrt(R_side2);
  R_long_EM = sqrt(R_long2);

  cout << "dN/(dypTdpTdphi) = " << scientific << setw(12) << setprecision(6) << spectra << "  1/(GeV^2)." << endl;
  cout << "R_out = " << R_out_EM << "  fm." << endl;
  cout << "R_side = " << R_side_EM << "  fm." << endl;
  cout << "R_long = " << R_long_EM << "  fm." << endl;

  return;
}                                         	


void HBT::Cal_correlationfunction_1D()
{
   if(fabs(K_y) > 1e-16)
   {
       cout<<"HBT:: not support for y not equals 0 yet!" << endl;
       return;
   }
   
   double mass = particle_ptr[particle_id].mass;
   double local_K_T = K_T;
   double localK_phi = K_phi;
   double cosK_phi = cos(localK_phi);
   double sinK_phi = sin(localK_phi);
   double error = 1e-4;

   cout << "generating the 1d slices of the correlation function along q_out, q_side, and q_long direction... " << endl;
		  
   for(int i = 0; i < qnpts; i++)
   {
      cout << "calculating q_mu = " << q_out[i] << endl;
      double values[3];
      for (int ops = 0; ops < 3; ops++)
         values[ops] = 0.0;
      for (int l = 0; l < 3; l++)
      {
         double local_q_out=0.0, local_q_side=0.0, local_q_long=0.0;
         switch (l)
         {
            case 0:
            {
               local_q_out  = q_out[i];
               local_q_side = 0.0e0;
               local_q_long = 0.0e0;
               break;
            }
   	      case 1:
            {
               local_q_out  = 0.0e0;
               local_q_side = q_side[i];
               local_q_long = 0.0e0;
               break;
            }
            case 2:
            {
               local_q_out  = 0.0e0;
               local_q_side = 0.0e0;
               local_q_long = q_long[i];
               break;
            }
            default:
            {
               cout << "error in assigning q values! "<< endl;
               break;
            }
         }

     	   double xsi  = local_K_T*local_K_T + mass*mass + (local_q_out*local_q_out + local_q_side*local_q_side + local_q_long*local_q_long)/4.0;  //Set Xsi
         double E1sq = xsi + local_K_T*local_q_out;
         double E2sq = xsi - local_K_T*local_q_out;
         double qt = sqrt(E1sq) - sqrt(E2sq);
         double qx = local_q_out*cosK_phi - local_q_side*sinK_phi;
         double qy = local_q_side*cosK_phi + local_q_out*sinK_phi;
         double qz = local_q_long;

         double integ1 = 0.0;                         //integ## are the numerators for the different q=const curves ("a" varies q_o, "b" varies q_s and "c" varies q_l) 
         double integ2 = 0.0;

         for(int k = 0; k < Emissionfunction_length; k++)
         {
            double ss  = Emissionfunction_Data[k];
            double tpt = Emissionfunction_t[k];
            double xpt = Emissionfunction_x[k];
            double ypt = Emissionfunction_y[k];
            double zpt = Emissionfunction_z[k];
            
            for(int ii=0; ii<2; ii++)
            {
               zpt = zpt*(-1);   //using the symmetry along z axis
               double arg = (tpt*qt - (qx*xpt + qy*ypt + qz*zpt))/hbarC;
               integ1 += cos(arg)*ss;
               integ2 += sin(arg)*ss;
            }
         }
         integ1 = integ1/hbarC/hbarC/hbarC/spectra;
         integ2 = integ2/hbarC/hbarC/hbarC/spectra;
         double localvalue = integ1*integ1+integ2*integ2;
         values[l] = localvalue;
      }
      Correl_1D_out[i]  = values[0];
      Correl_1D_side[i] = values[1];
      Correl_1D_long[i] = values[2];
      Correl_1D_out_err[i] = error;
      Correl_1D_side_err[i] = error;
      Correl_1D_long_err[i] = error;
   }
   return;
} 

void HBT::Cal_correlationfunction_3D()
{
   if(fabs(K_y) > 1e-16)
   {
       cout<<"not support for y not equals 0 yet!" << endl;
       return;
   }

   double mass = particle_ptr[particle_id].mass;
   double local_K_T = K_T;
   double localK_phi = K_phi;
   double cosK_phi = cos(localK_phi);
   double sinK_phi = sin(localK_phi);
   double error = 1e-4;

   cout << "generating correlation function in 3D... " << endl;
   for(int i = 0; i < qnpts; i++)  // q_out loop
   {
      double local_q_out = q_out[i];
      cout << "q_out = " << local_q_out << endl;
      for(int j = 0; j < qnpts; j++)  // q_side loop
      {
         double local_q_side = q_side[j];
         for(int k = 0; k < qnpts; k++)  // q_long loop
         {
            double local_q_long = q_long[k];
            double integ1 = 0.0;                         
            double integ2 = 0.0;
            double sum = 0.0;

     	      double xsi = local_K_T*local_K_T + mass*mass + (local_q_out*local_q_out + local_q_side*local_q_side + local_q_long*local_q_long)/4.0;  //Set Xsi
            double E1sq = xsi + local_K_T*local_q_out;
            double E2sq = xsi - local_K_T*local_q_out;
            double qt = sqrt(E1sq) - sqrt(E2sq);
            double qx = local_q_out*cosK_phi - local_q_side*sinK_phi;
            double qy = local_q_side*cosK_phi + local_q_out*sinK_phi;
            double qz = local_q_long;
            
            for(int m = 0; m < Emissionfunction_length; m++)
            {
               double ss = Emissionfunction_Data[m];
               double tpt = Emissionfunction_t[m];
               double xpt = Emissionfunction_x[m];
               double ypt = Emissionfunction_y[m];
               double zpt = Emissionfunction_z[m];

               for(int ii=0; ii<2; ii++)
               {
                  zpt = zpt*(-1);
               
                  double arg = (tpt*qt - (qx*xpt + qy*ypt + qz*zpt))/hbarC;
                  integ1 += cos(arg)*ss;
                  integ2 += sin(arg)*ss;
               }
            }
            integ1 = integ1/hbarC/hbarC/hbarC/spectra;
            integ2 = integ2/hbarC/hbarC/hbarC/spectra;
            sum = integ1*integ1+integ2*integ2;
            Correl_3D[i][j][k] = sum;
            Correl_3D_err[i][j][k] = error;
         }
      }
   }
   return;
}

void HBT::Cal_correlationfunction_1D_MC()
{
   if(fabs(K_y) > 1e-16)
   {
       cout<<"not support for y not equals 0 yet!" << endl;
       return;
   }
   
   double mass = particle_ptr[particle_id].mass;
   double local_K_T = K_T;
   double localK_phi = K_phi;
   double cosK_phi = cos(localK_phi);
   double sinK_phi = sin(localK_phi);

   cout << "generating the 1d slices of the correlation function along q_out, q_side, and q_long direction... " << endl;
   cout << "using Monte-Carlo intergration ..." << endl;
		  
   unsigned long int seed;
   double Ran_lowerlimit = 0.;
   double Ran_upperlimit = 1.;
   double randnum;
   gsl_rng *rng_ptr;
   rng_ptr = gsl_rng_alloc (gsl_rng_taus);
   seed = random_seed(-1);
   gsl_rng_set(rng_ptr, seed);

   for(int i = 0; i < qnpts; i++)
   {
      cout << "calculaing q_mu = " << q_out[i] << endl;
      double values[3];
      double values_err[3];
      for (int ops = 0; ops < 3; ops++)
      {
         values[ops] = 0.0;
         values_err[ops] = 0.0;
      }
      for (int l = 0; l < 3; l++)
      {
         double local_q_out=0.0, local_q_side=0.0, local_q_long=0.0;
         switch (l)
         {
            case 0:
            {
               local_q_out = q_out[i];
               local_q_side = 0.0e0;
               local_q_long = 0.0e0;
               break;
            }
   	   case 1:
            {
               local_q_out = 0.0e0;
               local_q_side = q_side[i];
               local_q_long = 0.0e0;
               break;
            }
            case 2:
            {
               local_q_out = 0.0e0;
               local_q_side = 0.0e0;
               local_q_long = q_long[i];
               break;
            }
            default:
            {
               cout << "error in assigning q values! "<< endl;
               break;
            }
         }

     	   double xsi = local_K_T*local_K_T + mass*mass + (local_q_out*local_q_out + local_q_side*local_q_side + local_q_long*local_q_long)/4.0;  //Set Xsi
         double E1sq = xsi + local_K_T*local_q_out;
         double E2sq = xsi - local_K_T*local_q_out;
         double qt = sqrt(E1sq) - sqrt(E2sq);
         double qx = local_q_out*cosK_phi - local_q_side*sinK_phi;
         double qy = local_q_side*cosK_phi + local_q_out*sinK_phi;
         double qz = local_q_long;

         double integ1 = 0.0e0;         //integ## are the numerators for the different q=const curves ("a" varies q_o, "b" varies q_s and "c" varies q_l) 
         double integ2 = 0.0e0;
         double integ3 = 0.0e0;
         double integ4 = 0.0e0;
         
         for(int k = 0; k < MCint_calls; k++)
         {
               randnum = gsl_ran_flat(rng_ptr, Ran_lowerlimit, Ran_upperlimit);
               int takepoint = binary_search(Emissionfunction_Data_CDF, Emissionfunction_length, randnum);

               double tpt = Emissionfunction_t[takepoint];
               double xpt = Emissionfunction_x[takepoint];
               double ypt = Emissionfunction_y[takepoint];
               double zpt = Emissionfunction_z[takepoint];
               double randsign = (rand()%2)*2-1;
               zpt = zpt*randsign;       //assume reflection symmetry along z axis
               
               double arg = (tpt*qt - (qx*xpt + qy*ypt + qz*zpt))/hbarC;
               double cos_arg = cos(arg);
               double sin_arg = sin(arg);
               integ1 += cos_arg;
               integ2 += sin_arg;
               integ3 += cos_arg*cos_arg;
               integ4 += sin_arg*sin_arg;
         }
         integ1 = integ1/((double)MCint_calls);
         integ2 = integ2/((double)MCint_calls);
         integ3 = integ3/((double)MCint_calls);
         integ4 = integ4/((double)MCint_calls);

         //MCint_calls-1 is used instead of MCint_calls in order to get an unbiased estimate of the variance
         double cov_Re = sqrt((integ3 - integ1*integ1)/(MCint_calls-1));
         double cov_Im = sqrt((integ4 - integ2*integ2)/(MCint_calls-1));

         double localvalue = integ1*integ1+integ2*integ2;
         //double localvalue_err = 2*sqrt(localvalue)*(cov_Re*cov_Re + cov_Im*cov_Im)/sqrt(MCint_calls);
         double localvalue_err = 2*(fabs(integ1)*cov_Re + fabs(integ2)*cov_Im);
         values[l] = localvalue;
         values_err[l] = localvalue_err;
      }
      Correl_1D_out[i]  = values[0];
      Correl_1D_side[i] = values[1];
      Correl_1D_long[i] = values[2];
      Correl_1D_out_err[i] = values_err[0];
      Correl_1D_side_err[i] = values_err[1];
      Correl_1D_long_err[i] = values_err[2];
   }
   return;
} 

void HBT::Cal_correlationfunction_3D_MC()
{
   if(fabs(K_y) > 1e-16)
   {
       cout<<"not support for y not equals 0 yet!" << endl;
       return;
   }
   
   double mass = particle_ptr[particle_id].mass;
   double local_K_T = K_T;
   double localK_phi = K_phi;
   double cosK_phi = cos(localK_phi);
   double sinK_phi = sin(localK_phi);

   cout << "generating correlation function in 3D... " << endl;
   cout << "using Monte-Carlo integration ... " << endl;
   
   unsigned long int seed;
   double Ran_lowerlimit = 0.;
   double Ran_upperlimit = 1.;
   double randnum;
   gsl_rng *rng_ptr;
   rng_ptr = gsl_rng_alloc (gsl_rng_taus);
   seed = random_seed(-1);
   gsl_rng_set(rng_ptr, seed);

   for(int i = 0; i < qnpts; i++)  // q_out loop
   {
      double local_q_out = q_out[i];
      cout << "q_out = " << local_q_out << endl;
      for(int j = 0; j < qnpts; j++)  // q_side loop
      {
         double local_q_side = q_side[j];
         for(int k = 0; k < qnpts; k++)  // q_long loop
         {
            double local_q_long = q_long[k];
            double integ1 = 0.0;                         
            double integ2 = 0.0;
            double integ3 = 0.0e0;
            double integ4 = 0.0e0;
     	      
            double xsi = local_K_T*local_K_T + mass*mass + (local_q_out*local_q_out + local_q_side*local_q_side + local_q_long*local_q_long)/4.0;  //Set Xsi
            double E1sq = xsi + local_K_T*local_q_out;
            double E2sq = xsi - local_K_T*local_q_out;
            double qt = sqrt(E1sq) - sqrt(E2sq);
            double qx = local_q_out*cosK_phi - local_q_side*sinK_phi;
            double qy = local_q_side*cosK_phi + local_q_out*sinK_phi;
            double qz = local_q_long;

            for(int m = 0; m < MCint_calls; m++)
            {
                randnum = gsl_ran_flat(rng_ptr, Ran_lowerlimit, Ran_upperlimit);
                int takepoint = binary_search(Emissionfunction_Data_CDF, Emissionfunction_length, randnum);
                double tpt = Emissionfunction_t[takepoint];
                double xpt = Emissionfunction_x[takepoint];
                double ypt = Emissionfunction_y[takepoint];
                double zpt = Emissionfunction_z[takepoint];
                double randsign = (rand()%2)*2-1;
                zpt = zpt*randsign;       //assume reflection symmetry along z axis
     	          
                double arg = (tpt*qt - (qx*xpt + qy*ypt + qz*zpt))/hbarC;
                double cos_arg = cos(arg);
                double sin_arg = sin(arg);
                integ1 += cos_arg;
                integ2 += sin_arg;
                integ3 += cos_arg*cos_arg;
                integ4 += sin_arg*sin_arg;
     	      }
            integ1 = integ1/((double)MCint_calls);
            integ2 = integ2/((double)MCint_calls);
            integ3 = integ3/((double)MCint_calls);
            integ4 = integ4/((double)MCint_calls);

            double sum = integ1*integ1+integ2*integ2;
            //MCint_calls-1 is used instead of MCint_calls in order to get an unbiased estimate of the variance
            double cov_Re = sqrt((integ3 - integ1*integ1)/(MCint_calls-1));
            double cov_Im = sqrt((integ4 - integ2*integ2)/(MCint_calls-1));
            
            //double sum_err = 2*sqrt(sum)*(cov_Re*cov_Re + cov_Im*cov_Im)/sqrt(MCint_calls);
            double sum_err = 2*(fabs(integ1)*cov_Re + fabs(integ2)*cov_Im);

            Correl_3D[i][j][k] = sum;
            Correl_3D_err[i][j][k] = sum_err;
         }
      }
   }
}

int HBT::binary_search(double* dataset, int data_length, double value)
{
  int lowbin = 0;
  int midbin = 0;
  int highbin = data_length;
  int stop = 0;
  int dbin = highbin - lowbin;
  if (dbin == 0) 
  {
     cout << "binary search::You screwed up in the table lookup dummy. Fix it." << endl;
     exit(1);
  }

  while(stop == 0) 
  {
      dbin = highbin - lowbin;
      midbin = (int)(lowbin + dbin/2);
      if(dbin == 1)
      {
	  stop = 1;
      }
      else if(value > dataset[midbin])
      {
	  lowbin = midbin;
      }
      else
      {
	  highbin = midbin;
      }
  }
  return(lowbin);
}


void HBT::Output_Correlationfunction_1D()
{
   ostringstream oCorrelfun_1D_stream;
   oCorrelfun_1D_stream << path << "/correlfunct1D" << "_" << particle_ptr[particle_id].name << "_kt_" << K_T << "_phi_" << K_phi << ".dat";
   ofstream oCorrelfun_1D;
   oCorrelfun_1D.open(oCorrelfun_1D_stream.str().c_str());
   for(int i=0; i < qnpts; i++)
     oCorrelfun_1D << scientific << setprecision(7) << setw(15)
                   << q_out[i] << "  " << Correl_1D_out[i] << "  " << Correl_1D_out_err[i] << "  "
                   << q_side[i] << "  " << Correl_1D_side[i] << "  " << Correl_1D_side_err[i] << "  "
                   << q_long[i] << "  " << Correl_1D_long[i] << "  " << Correl_1D_long_err[i] 
                   << endl;
   return;
}

void HBT::Output_Correlationfunction_3D()
{
   ostringstream oCorrelfun_3D_stream;
   oCorrelfun_3D_stream << path << "/correlfunct3D" << "_" << particle_ptr[particle_id].name << "_kt_" << K_T << "_phi_" << K_phi << ".dat";
   ofstream oCorrelfun_3D;
   oCorrelfun_3D.open(oCorrelfun_3D_stream.str().c_str());
   for(int i=0; i < qnpts; i++)
      for(int j=0; j < qnpts; j++)
         for(int k=0; k < qnpts; k++)
              oCorrelfun_3D << scientific << setprecision(7) << setw(15)
                            << q_out[i] << "  " << q_side[j] << "  " 
                            << q_long[k] << "  " << Correl_3D[i][j][k] << "  "
                            << Correl_3D_err[i][j][k] << endl;
   return;
}



//*********************************************************************
// Functions used for multidimension fit
void HBT::Fit_Correlationfunction1D()
{
  const int data_length = qnpts;  // # of points
  const size_t n_para = 2;  // # of parameters

  // allocate space for a covariance matrix of size p by p
  gsl_matrix *covariance_ptr = gsl_matrix_alloc (n_para, n_para);

  // allocate and setup for generating gaussian distibuted random numbers
  gsl_rng_env_setup ();
  const gsl_rng_type *type = gsl_rng_default;
  gsl_rng *rng_ptr = gsl_rng_alloc (type);

  //set up test data
  struct Correlationfunction1D_data Correlfun1D_data;
  Correlfun1D_data.data_length = data_length;
  Correlfun1D_data.q = new double [data_length];
  Correlfun1D_data.y = new double [data_length];
  Correlfun1D_data.sigma = new double [data_length];

  for(int i=0; i<data_length; i++)
  {
     Correlfun1D_data.q[i] = q_out[i];
     // This sets up the data to be fitted, with gaussian noise added
     //Correlfun1D_data.y[i] = 1.0*exp(-10*q_out[i]*q_out[i]) +gsl_ran_gaussian(rng_ptr, error);
     Correlfun1D_data.y[i] = Correl_1D_out[i];
     Correlfun1D_data.sigma[i] = Correl_1D_out_err[i];
     //cout << Correlfun1D_data.q[i] << "  " << Correlfun1D_data.y[i] << "  " << Correlfun1D_data.sigma[i] << endl;
  }

  double para_init[n_para] = {1.0, 1.0};  // initial guesse of parameters

  gsl_vector_view xvec_ptr = gsl_vector_view_array (para_init, n_para);
  
  // set up the function to be fit 
  gsl_multifit_function_fdf target_func;
  target_func.f = &Fittarget_correlfun1D_f;        // the function of residuals
  target_func.df = &Fittarget_correlfun1D_df;      // the gradient of this function
  target_func.fdf = &Fittarget_correlfun1D_fdf;    // combined function and gradient
  target_func.n = data_length;              // number of points in the data set
  target_func.p = n_para;              // number of parameters in the fit function
  target_func.params = &Correlfun1D_data;  // structure with the data and error bars

  const gsl_multifit_fdfsolver_type *type_ptr = gsl_multifit_fdfsolver_lmsder;
  gsl_multifit_fdfsolver *solver_ptr 
       = gsl_multifit_fdfsolver_alloc (type_ptr, data_length, n_para);
  gsl_multifit_fdfsolver_set (solver_ptr, &target_func, &xvec_ptr.vector);

  size_t iteration = 0;         // initialize iteration counter
  print_fit_state_1D (iteration, solver_ptr);
  int status;  		// return value from gsl function calls (e.g., error)
  do
  {
      iteration++;
      
      // perform a single iteration of the fitting routine
      status = gsl_multifit_fdfsolver_iterate (solver_ptr);

      // print out the status of the fit
      cout << "status = " << gsl_strerror (status) << endl;

      // customized routine to print out current parameters
      print_fit_state_1D (iteration, solver_ptr);

      if (status)    // check for a nonzero status code
      {
          break;  // this should only happen if an error code is returned 
      }

      // test for convergence with an absolute and relative error (see manual)
      status = gsl_multifit_test_delta (solver_ptr->dx, solver_ptr->x, 
                                        fit_tolarence, fit_tolarence);
  }
  while (status == GSL_CONTINUE && iteration < fit_max_iterations);

  // calculate the covariance matrix of the best-fit parameters
  gsl_multifit_covar (solver_ptr->J, 0.0, covariance_ptr);

  // print out the covariance matrix using the gsl function (not elegant!)
  cout << endl << "Covariance matrix: " << endl;
  gsl_matrix_fprintf (stdout, covariance_ptr, "%g");

  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		                // # of digits in doubles

  int width = 7;		// setw width for output
  cout << endl << "Best fit results:" << endl;
  cout << "lambda = " << setw (width) << get_fit_results (0, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (0, covariance_ptr) << endl;

  cout << "R = " << setw (width) << get_fit_results (1, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (1, covariance_ptr) << endl;

  cout << "status = " << gsl_strerror (status) << endl;
  cout << "------------------------------------------------------------------" << endl;

  double chi = gsl_blas_dnrm2(solver_ptr->f);
  double dof = data_length - n_para;
  double c = GSL_MAX_DBL(1, chi/sqrt(dof));

  lambda_Correl = get_fit_results(0, solver_ptr);
  R_out_Correl = get_fit_results(1, solver_ptr)*hbarC;
  lambda_Correl_err = c*get_fit_err(0, covariance_ptr);
  R_out_Correl_err = c*get_fit_err(1, covariance_ptr)*hbarC;

  cout << "final results: " << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << "chisq/dof = " << chi*chi/dof << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << " lambda = " << lambda_Correl << " +/- " << lambda_Correl_err << endl;
  cout << " R_out = " << R_out_Correl << " +/- " << R_out_Correl_err << endl;

  //clean up
  gsl_matrix_free (covariance_ptr);
  gsl_rng_free (rng_ptr);

  delete[] Correlfun1D_data.q;
  delete[] Correlfun1D_data.y;
  delete[] Correlfun1D_data.sigma;

  gsl_multifit_fdfsolver_free (solver_ptr);  // free up the solver

  return;
}

void HBT::Fit_Correlationfunction3D()
{
  const size_t data_length = qnpts*qnpts*qnpts;  // # of points
  const size_t n_para = 4;  // # of parameters

  // allocate space for a covariance matrix of size p by p
  gsl_matrix *covariance_ptr = gsl_matrix_alloc (n_para, n_para);

  // allocate and setup for generating gaussian distibuted random numbers
  gsl_rng_env_setup ();
  const gsl_rng_type *type = gsl_rng_default;
  gsl_rng *rng_ptr = gsl_rng_alloc (type);

  //set up test data
  struct Correlationfunction3D_data Correlfun3D_data;
  Correlfun3D_data.data_length = data_length;
  Correlfun3D_data.q_o = new double [data_length];
  Correlfun3D_data.q_s = new double [data_length];
  Correlfun3D_data.q_l = new double [data_length];
  Correlfun3D_data.y = new double [data_length];
  Correlfun3D_data.sigma = new double [data_length];

  int idx = 0;
  for(int i=0; i<qnpts; i++)
  {
    for(int j=0; j<qnpts; j++)
    {
      for(int k=0; k<qnpts; k++)
      {
         Correlfun3D_data.q_o[idx] = q_out[i];
         Correlfun3D_data.q_s[idx] = q_side[j];
         Correlfun3D_data.q_l[idx] = q_long[k];
         // This sets up the data to be fitted, with gaussian noise added
         // Correlfun3D_data.y[idx] = 1.0*exp( - 0.81*q_out[i]*q_out[i] - 1.21*q_side[j]*q_side[j] - 4.0*q_long[k]*q_long[k] - 0.25*q_out[i]*q_side[j]) + gsl_ran_gaussian(rng_ptr, error);
         Correlfun3D_data.y[idx] = Correl_3D[i][j][k];
         Correlfun3D_data.sigma[idx] = Correl_3D_err[i][j][k];
         //Correlfun3D_data.sigma[idx] = 1e-2;
         idx++;
      }
    }
  }

  double para_init[n_para] = { 1.0, 1.0, 1.0, 1.0 };  // initial guesse of parameters

  gsl_vector_view xvec_ptr = gsl_vector_view_array (para_init, n_para);
  
  // set up the function to be fit 
  gsl_multifit_function_fdf target_func;
  target_func.f = &Fittarget_correlfun3D_f;        // the function of residuals
  target_func.df = &Fittarget_correlfun3D_df;      // the gradient of this function
  target_func.fdf = &Fittarget_correlfun3D_fdf;    // combined function and gradient
  target_func.n = data_length;              // number of points in the data set
  target_func.p = n_para;              // number of parameters in the fit function
  target_func.params = &Correlfun3D_data;  // structure with the data and error bars

  const gsl_multifit_fdfsolver_type *type_ptr = gsl_multifit_fdfsolver_lmsder;
  gsl_multifit_fdfsolver *solver_ptr 
       = gsl_multifit_fdfsolver_alloc (type_ptr, data_length, n_para);
  gsl_multifit_fdfsolver_set (solver_ptr, &target_func, &xvec_ptr.vector);

  size_t iteration = 0;         // initialize iteration counter
  print_fit_state_3D (iteration, solver_ptr);
  int status;  		// return value from gsl function calls (e.g., error)
  do
  {
      iteration++;
      
      // perform a single iteration of the fitting routine
      status = gsl_multifit_fdfsolver_iterate (solver_ptr);

      // print out the status of the fit
      cout << "status = " << gsl_strerror (status) << endl;

      // customized routine to print out current parameters
      print_fit_state_3D (iteration, solver_ptr);

      if (status)    // check for a nonzero status code
      {
          break;  // this should only happen if an error code is returned 
      }

      // test for convergence with an absolute and relative error (see manual)
      status = gsl_multifit_test_delta (solver_ptr->dx, solver_ptr->x, 
                                        fit_tolarence, fit_tolarence);
  }
  while (status == GSL_CONTINUE && iteration < fit_max_iterations);

  // calculate the covariance matrix of the best-fit parameters
  gsl_multifit_covar (solver_ptr->J, 0.0, covariance_ptr);

  // print out the covariance matrix using the gsl function (not elegant!)
  cout << endl << "Covariance matrix: " << endl;
  gsl_matrix_fprintf (stdout, covariance_ptr, "%g");

  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		                // # of digits in doubles

  int width = 7;		// setw width for output
  cout << endl << "Best fit results:" << endl;
  cout << "Ro = " << setw (width) << get_fit_results (0, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (0, covariance_ptr) << endl;

  cout << "Rs      = " << setw (width) << get_fit_results (1, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (1, covariance_ptr) << endl;

  cout << "Rl      = " << setw (width) << get_fit_results (2, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (2, covariance_ptr) << endl;
  
  cout << "Ros      = " << setw (width) << get_fit_results (3, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (3, covariance_ptr) << endl;
    
  cout << "status = " << gsl_strerror (status) << endl;
  cout << "--------------------------------------------------------------------" << endl;

  double chi = gsl_blas_dnrm2(solver_ptr->f);
  double dof = data_length - n_para;
  double c = GSL_MAX_DBL(1, chi/sqrt(dof));

  lambda_Correl = 1.0;
  lambda_Correl_err = 0.0;
  R_out_Correl = fabs(get_fit_results(0, solver_ptr))*hbarC;
  R_side_Correl = fabs(get_fit_results(1, solver_ptr))*hbarC;
  R_long_Correl = fabs(get_fit_results(2, solver_ptr))*hbarC;
  R_os_Correl = fabs(get_fit_results(3, solver_ptr))*hbarC;
  R_out_Correl_err = c*get_fit_err(0, covariance_ptr)*hbarC;
  R_side_Correl_err = c*get_fit_err(1, covariance_ptr)*hbarC;
  R_long_Correl_err = c*get_fit_err(2, covariance_ptr)*hbarC;
  R_os_Correl_err = c*get_fit_err(3, covariance_ptr)*hbarC;

  cout << "final results: " << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << "chisq/dof = " << chi*chi/dof << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << " lambda = " << lambda_Correl << " +/- " << lambda_Correl_err << endl;
  cout << " R_out = " << R_out_Correl << " +/- " << R_out_Correl_err << endl;
  cout << " R_side = " << R_side_Correl << " +/- " << R_side_Correl_err << endl;
  cout << " R_long = " << R_long_Correl << " +/- " << R_long_Correl_err << endl;
  cout << " R_os = " << R_os_Correl << " +/- " << R_os_Correl_err << endl;

  //clean up
  gsl_matrix_free (covariance_ptr);
  gsl_rng_free (rng_ptr);

  delete[] Correlfun3D_data.q_o;
  delete[] Correlfun3D_data.q_s;
  delete[] Correlfun3D_data.q_l;
  delete[] Correlfun3D_data.y;
  delete[] Correlfun3D_data.sigma;

  gsl_multifit_fdfsolver_free (solver_ptr);  // free up the solver

  return;
}

void HBT::Fit_Correlationfunction3D_withlambda()
{
  const size_t data_length = qnpts*qnpts*qnpts;  // # of points
  const size_t n_para = 5;  // # of parameters

  // allocate space for a covariance matrix of size p by p
  gsl_matrix *covariance_ptr = gsl_matrix_alloc (n_para, n_para);

  // allocate and setup for generating gaussian distibuted random numbers
  gsl_rng_env_setup ();
  const gsl_rng_type *type = gsl_rng_default;
  gsl_rng *rng_ptr = gsl_rng_alloc (type);

  //set up test data
  struct Correlationfunction3D_data Correlfun3D_data;
  Correlfun3D_data.data_length = data_length;
  Correlfun3D_data.q_o = new double [data_length];
  Correlfun3D_data.q_s = new double [data_length];
  Correlfun3D_data.q_l = new double [data_length];
  Correlfun3D_data.y = new double [data_length];
  Correlfun3D_data.sigma = new double [data_length];

  int idx = 0;
  for(int i=0; i<qnpts; i++)
  {
    for(int j=0; j<qnpts; j++)
    {
      for(int k=0; k<qnpts; k++)
      {
         Correlfun3D_data.q_o[idx] = q_out[i];
         Correlfun3D_data.q_s[idx] = q_side[j];
         Correlfun3D_data.q_l[idx] = q_long[k];
         // This sets up the data to be fitted, with gaussian noise added
         // Correlfun3D_data.y[idx] = 1.0*exp( - 0.81*q_out[i]*q_out[i] - 1.21*q_side[j]*q_side[j] - 4.0*q_long[k]*q_long[k] - 0.25*q_out[i]*q_side[j]) + gsl_ran_gaussian(rng_ptr, error);
         Correlfun3D_data.y[idx] = Correl_3D[i][j][k];
         Correlfun3D_data.sigma[idx] = Correl_3D_err[i][j][k];
         idx++;
      }
    }
  }

  double para_init[n_para] = { 1.0, 1.0, 1.0, 1.0, 1.0 };  // initial guesse of parameters

  gsl_vector_view xvec_ptr = gsl_vector_view_array (para_init, n_para);
  
  // set up the function to be fit 
  gsl_multifit_function_fdf target_func;
  target_func.f = &Fittarget_correlfun3D_f_withlambda;        // the function of residuals
  target_func.df = &Fittarget_correlfun3D_df_withlambda;      // the gradient of this function
  target_func.fdf = &Fittarget_correlfun3D_fdf_withlambda;    // combined function and gradient
  target_func.n = data_length;              // number of points in the data set
  target_func.p = n_para;              // number of parameters in the fit function
  target_func.params = &Correlfun3D_data;  // structure with the data and error bars

  const gsl_multifit_fdfsolver_type *type_ptr = gsl_multifit_fdfsolver_lmsder;
  gsl_multifit_fdfsolver *solver_ptr 
       = gsl_multifit_fdfsolver_alloc (type_ptr, data_length, n_para);
  gsl_multifit_fdfsolver_set (solver_ptr, &target_func, &xvec_ptr.vector);

  size_t iteration = 0;         // initialize iteration counter
  print_fit_state_3D (iteration, solver_ptr);
  int status;  		// return value from gsl function calls (e.g., error)
  do
  {
      iteration++;
      
      // perform a single iteration of the fitting routine
      status = gsl_multifit_fdfsolver_iterate (solver_ptr);

      // print out the status of the fit
      cout << "status = " << gsl_strerror (status) << endl;

      // customized routine to print out current parameters
      print_fit_state_3D (iteration, solver_ptr);

      if (status)    // check for a nonzero status code
      {
          break;  // this should only happen if an error code is returned 
      }

      // test for convergence with an absolute and relative error (see manual)
      status = gsl_multifit_test_delta (solver_ptr->dx, solver_ptr->x, 
                                        fit_tolarence, fit_tolarence);
  }
  while (status == GSL_CONTINUE && iteration < fit_max_iterations);

  // calculate the covariance matrix of the best-fit parameters
  gsl_multifit_covar (solver_ptr->J, 0.0, covariance_ptr);

  // print out the covariance matrix using the gsl function (not elegant!)
  cout << endl << "Covariance matrix: " << endl;
  gsl_matrix_fprintf (stdout, covariance_ptr, "%g");

  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		                // # of digits in doubles

  int width = 7;		// setw width for output
  cout << endl << "Best fit results:" << endl;
  cout << "lambda      = " << setw (width) << get_fit_results (0, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (0, covariance_ptr) << endl;

  cout << "Ro = " << setw (width) << get_fit_results (1, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (1, covariance_ptr) << endl;

  cout << "Rs      = " << setw (width) << get_fit_results (2, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (2, covariance_ptr) << endl;

  cout << "Rl      = " << setw (width) << get_fit_results (3, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (3, covariance_ptr) << endl;
  
  cout << "Ros      = " << setw (width) << get_fit_results (4, solver_ptr)
    << " +/- " << setw (width) << get_fit_err (4, covariance_ptr) << endl;
    
  cout << "status = " << gsl_strerror (status) << endl;
  cout << "--------------------------------------------------------------------" << endl;

  double chi = gsl_blas_dnrm2(solver_ptr->f);
  double dof = data_length - n_para;
  double c = GSL_MAX_DBL(1, chi/sqrt(dof));

  lambda_Correl = get_fit_results(0, solver_ptr);
  R_out_Correl = fabs(get_fit_results(1, solver_ptr))*hbarC;
  R_side_Correl = fabs(get_fit_results(2, solver_ptr))*hbarC;
  R_long_Correl = fabs(get_fit_results(3, solver_ptr))*hbarC;
  R_os_Correl = fabs(get_fit_results(4, solver_ptr))*hbarC;
  lambda_Correl_err = c*get_fit_err(0, covariance_ptr);
  R_out_Correl_err = c*get_fit_err(1, covariance_ptr)*hbarC;
  R_side_Correl_err = c*get_fit_err(2, covariance_ptr)*hbarC;
  R_long_Correl_err = c*get_fit_err(3, covariance_ptr)*hbarC;
  R_os_Correl_err = c*get_fit_err(4, covariance_ptr)*hbarC;

  cout << "final results: " << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << "chisq/dof = " << chi*chi/dof << endl;
  cout << scientific << setw(10) << setprecision(5) 
       << " lambda = " << lambda_Correl << " +/- " << lambda_Correl_err << endl;
  cout << " R_out = " << R_out_Correl << " +/- " << R_out_Correl_err << endl;
  cout << " R_side = " << R_side_Correl << " +/- " << R_side_Correl_err << endl;
  cout << " R_long = " << R_long_Correl << " +/- " << R_long_Correl_err << endl;
  cout << " R_os = " << R_os_Correl << " +/- " << R_os_Correl_err << endl;

  //clean up
  gsl_matrix_free (covariance_ptr);
  gsl_rng_free (rng_ptr);

  delete[] Correlfun3D_data.q_o;
  delete[] Correlfun3D_data.q_s;
  delete[] Correlfun3D_data.q_l;
  delete[] Correlfun3D_data.y;
  delete[] Correlfun3D_data.sigma;

  gsl_multifit_fdfsolver_free (solver_ptr);  // free up the solver

  return;
}

//*********************************************************************
// 1D case
//*********************************************************************
//  Simple function to print results of each iteration in nice format
int HBT::print_fit_state_1D (size_t iteration, gsl_multifit_fdfsolver * solver_ptr)
{
  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		// digits in doubles

  int width = 15;		// setw width for output
  cout << scientific
    << "iteration " << iteration << ": "
    << "x = {" << setw (width) << gsl_vector_get (solver_ptr->x, 0)
    << setw (width) << gsl_vector_get (solver_ptr->x, 1)
    << "}, |f(x)| = " << scientific << gsl_blas_dnrm2 (solver_ptr->f) 
    << endl << endl;

  return 0;
}
//*********************************************************************
//  Function returning the residuals for each point; that is, the 
//  difference of the fit function using the current parameters
//  and the data to be fit.
int Fittarget_correlfun1D_f (const gsl_vector *xvec_ptr, void *params_ptr, gsl_vector *f_ptr)
{
  size_t n = ((struct Correlationfunction1D_data *) params_ptr)->data_length;
  double *q = ((struct Correlationfunction1D_data *) params_ptr)->q;
  double *y = ((struct Correlationfunction1D_data *) params_ptr)->y;
  double *sigma = ((struct Correlationfunction1D_data *) params_ptr)->sigma;

  //fit parameters
  double lambda = gsl_vector_get (xvec_ptr, 0);
  double R = gsl_vector_get (xvec_ptr, 1);

  size_t i;

  for (i = 0; i < n; i++)
  {
      double Yi = lambda*exp(- q[i]*q[i]*R*R);
      gsl_vector_set (f_ptr, i, (Yi - y[i]) / sigma[i]);
  }

  return GSL_SUCCESS;
}

//*********************************************************************
//  Function returning the Jacobian of the residual function
int Fittarget_correlfun1D_df (const gsl_vector *xvec_ptr, void *params_ptr,  gsl_matrix *Jacobian_ptr)
{
  size_t n = ((struct Correlationfunction1D_data *) params_ptr)->data_length;
  double *q = ((struct Correlationfunction1D_data *) params_ptr)->q;
  double *sigma = ((struct Correlationfunction1D_data *) params_ptr)->sigma;

  //fit parameters
  double lambda = gsl_vector_get (xvec_ptr, 0);
  double R = gsl_vector_get (xvec_ptr, 1);

  size_t i;

  for (i = 0; i < n; i++)
  {
      // Jacobian matrix J(i,j) = dfi / dxj, 
      // where fi = (Yi - yi)/sigma[i],      
      //       Yi = A * exp(-lambda * i) + b 
      // and the xj are the parameters (A,lambda,b) 
      double sig = sigma[i];

      //derivatives
      double common_elemt = exp(- q[i]*q[i]*R*R);
      
      gsl_matrix_set (Jacobian_ptr, i, 0, common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 1, - lambda*q[i]*q[i]*2.*R*common_elemt/sig);
  }
  return GSL_SUCCESS;
}

//*********************************************************************
//  Function combining the residual function and its Jacobian
int Fittarget_correlfun1D_fdf (const gsl_vector* xvec_ptr, void *params_ptr, gsl_vector* f_ptr, gsl_matrix* Jacobian_ptr)
{
  Fittarget_correlfun1D_f(xvec_ptr, params_ptr, f_ptr);
  Fittarget_correlfun1D_df(xvec_ptr, params_ptr, Jacobian_ptr);

  return GSL_SUCCESS;
}


//*********************************************************************
// 3D case
//*********************************************************************
//  Simple function to print results of each iteration in nice format
int HBT::print_fit_state_3D (size_t iteration, gsl_multifit_fdfsolver * solver_ptr)
{
  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		// digits in doubles

  int width = 15;		// setw width for output
  cout << scientific
    << "iteration " << iteration << ": "
    << "  x = {" << setw (width) << gsl_vector_get (solver_ptr->x, 0)
    << setw (width) << gsl_vector_get (solver_ptr->x, 1)
    << setw (width) << gsl_vector_get (solver_ptr->x, 2)
    << setw (width) << gsl_vector_get (solver_ptr->x, 3)
    << "}, |f(x)| = " << scientific << gsl_blas_dnrm2 (solver_ptr->f) 
    << endl << endl;

  return 0;
}
//  Simple function to print results of each iteration in nice format
int HBT::print_fit_state_3D_withlambda (size_t iteration, gsl_multifit_fdfsolver * solver_ptr)
{
  cout.setf (ios::fixed, ios::floatfield);	// output in fixed format
  cout.precision (5);		// digits in doubles

  int width = 15;		// setw width for output
  cout << scientific
    << "iteration " << iteration << ": "
    << "  x = {" << setw (width) << gsl_vector_get (solver_ptr->x, 0)
    << setw (width) << gsl_vector_get (solver_ptr->x, 1)
    << setw (width) << gsl_vector_get (solver_ptr->x, 2)
    << setw (width) << gsl_vector_get (solver_ptr->x, 3)
    << setw (width) << gsl_vector_get (solver_ptr->x, 4)
    << "}, |f(x)| = " << scientific << gsl_blas_dnrm2 (solver_ptr->f) 
    << endl << endl;

  return 0;
}
//*********************************************************************
//  Function returning the residuals for each point; that is, the 
//  difference of the fit function using the current parameters
//  and the data to be fit.
int Fittarget_correlfun3D_f (const gsl_vector *xvec_ptr, void *params_ptr, gsl_vector *f_ptr)
{
  size_t n = ((struct Correlationfunction3D_data *) params_ptr)->data_length;
  double *q_o = ((struct Correlationfunction3D_data *) params_ptr)->q_o;
  double *q_s = ((struct Correlationfunction3D_data *) params_ptr)->q_s;
  double *q_l = ((struct Correlationfunction3D_data *) params_ptr)->q_l;
  double *y = ((struct Correlationfunction3D_data *) params_ptr)->y;
  double *sigma = ((struct Correlationfunction3D_data *) params_ptr)->sigma;

  //fit parameters
  double R_o = gsl_vector_get (xvec_ptr, 0);
  double R_s = gsl_vector_get (xvec_ptr, 1);
  double R_l = gsl_vector_get (xvec_ptr, 2);
  double R_os = gsl_vector_get (xvec_ptr, 3);

  size_t i;

  for (i = 0; i < n; i++)
  {
      double Yi = exp(- q_l[i]*q_l[i]*R_l*R_l - q_s[i]*q_s[i]*R_s*R_s
                   - q_o[i]*q_o[i]*R_o*R_o - q_o[i]*q_s[i]*R_os*R_os);
      gsl_vector_set (f_ptr, i, (Yi - y[i]) / sigma[i]);
  }

  return GSL_SUCCESS;
}

int Fittarget_correlfun3D_f_withlambda (const gsl_vector *xvec_ptr, void *params_ptr, gsl_vector *f_ptr)
{
  size_t n = ((struct Correlationfunction3D_data *) params_ptr)->data_length;
  double *q_o = ((struct Correlationfunction3D_data *) params_ptr)->q_o;
  double *q_s = ((struct Correlationfunction3D_data *) params_ptr)->q_s;
  double *q_l = ((struct Correlationfunction3D_data *) params_ptr)->q_l;
  double *y = ((struct Correlationfunction3D_data *) params_ptr)->y;
  double *sigma = ((struct Correlationfunction3D_data *) params_ptr)->sigma;

  //fit parameters
  double lambda = gsl_vector_get (xvec_ptr, 0);
  double R_o = gsl_vector_get (xvec_ptr, 1);
  double R_s = gsl_vector_get (xvec_ptr, 2);
  double R_l = gsl_vector_get (xvec_ptr, 3);
  double R_os = gsl_vector_get (xvec_ptr, 4);

  size_t i;

  for (i = 0; i < n; i++)
  {
      double Yi = lambda*exp(- q_l[i]*q_l[i]*R_l*R_l - q_s[i]*q_s[i]*R_s*R_s
                   - q_o[i]*q_o[i]*R_o*R_o - q_o[i]*q_s[i]*R_os*R_os);
      gsl_vector_set (f_ptr, i, (Yi - y[i]) / sigma[i]);
  }

  return GSL_SUCCESS;
}

//*********************************************************************
//  Function returning the Jacobian of the residual function
int Fittarget_correlfun3D_df (const gsl_vector *xvec_ptr, void *params_ptr,  gsl_matrix *Jacobian_ptr)
{
  size_t n = ((struct Correlationfunction3D_data *) params_ptr)->data_length;
  double *q_o = ((struct Correlationfunction3D_data *) params_ptr)->q_o;
  double *q_s = ((struct Correlationfunction3D_data *) params_ptr)->q_s;
  double *q_l = ((struct Correlationfunction3D_data *) params_ptr)->q_l;
  double *sigma = ((struct Correlationfunction3D_data *) params_ptr)->sigma;

  //fit parameters
  double R_o = gsl_vector_get (xvec_ptr, 0);
  double R_s = gsl_vector_get (xvec_ptr, 1);
  double R_l = gsl_vector_get (xvec_ptr, 2);
  double R_os = gsl_vector_get (xvec_ptr, 3);

  size_t i;

  for (i = 0; i < n; i++)
  {
      // Jacobian matrix J(i,j) = dfi / dxj, 
      // where fi = (Yi - yi)/sigma[i],      
      //       Yi = A * exp(-lambda * i) + b 
      // and the xj are the parameters (A,lambda,b) 
      double sig = sigma[i];

      //derivatives
      double common_elemt = exp(- q_l[i]*q_l[i]*R_l*R_l - q_s[i]*q_s[i]*R_s*R_s
                   - q_o[i]*q_o[i]*R_o*R_o - q_o[i]*q_s[i]*R_os*R_os);
      
      gsl_matrix_set (Jacobian_ptr, i, 0, - q_o[i]*q_o[i]*2.0*R_o*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 1, - q_s[i]*q_s[i]*2.0*R_s*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 2, - q_l[i]*q_l[i]*2.0*R_l*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 3, - q_o[i]*q_s[i]*2.0*R_os*common_elemt/sig);
  }
  return GSL_SUCCESS;
}

int Fittarget_correlfun3D_df_withlambda (const gsl_vector *xvec_ptr, void *params_ptr,  gsl_matrix *Jacobian_ptr)
{
  size_t n = ((struct Correlationfunction3D_data *) params_ptr)->data_length;
  double *q_o = ((struct Correlationfunction3D_data *) params_ptr)->q_o;
  double *q_s = ((struct Correlationfunction3D_data *) params_ptr)->q_s;
  double *q_l = ((struct Correlationfunction3D_data *) params_ptr)->q_l;
  double *sigma = ((struct Correlationfunction3D_data *) params_ptr)->sigma;

  //fit parameters
  double lambda = gsl_vector_get (xvec_ptr, 0);
  double R_o = gsl_vector_get (xvec_ptr, 1);
  double R_s = gsl_vector_get (xvec_ptr, 2);
  double R_l = gsl_vector_get (xvec_ptr, 3);
  double R_os = gsl_vector_get (xvec_ptr, 4);

  size_t i;

  for (i = 0; i < n; i++)
  {
      // Jacobian matrix J(i,j) = dfi / dxj, 
      // where fi = (Yi - yi)/sigma[i],      
      //       Yi = A * exp(-lambda * i) + b 
      // and the xj are the parameters (A,lambda,b) 
      double sig = sigma[i];

      //derivatives
      double common_elemt = exp(- q_l[i]*q_l[i]*R_l*R_l - q_s[i]*q_s[i]*R_s*R_s
                   - q_o[i]*q_o[i]*R_o*R_o - q_o[i]*q_s[i]*R_os*R_os);
      
      gsl_matrix_set (Jacobian_ptr, i, 0, common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 1, - lambda*q_o[i]*q_o[i]*2.0*R_o*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 2, - lambda*q_s[i]*q_s[i]*2.0*R_s*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 3, - lambda*q_l[i]*q_l[i]*2.0*R_l*common_elemt/sig);
      gsl_matrix_set (Jacobian_ptr, i, 4, - lambda*q_o[i]*q_s[i]*2.0*R_os*common_elemt/sig);
  }
  return GSL_SUCCESS;
}

//*********************************************************************
//  Function combining the residual function and its Jacobian
int Fittarget_correlfun3D_fdf (const gsl_vector* xvec_ptr, void *params_ptr, gsl_vector* f_ptr, gsl_matrix* Jacobian_ptr)
{
  Fittarget_correlfun3D_f(xvec_ptr, params_ptr, f_ptr);
  Fittarget_correlfun3D_df(xvec_ptr, params_ptr, Jacobian_ptr);

  return GSL_SUCCESS;
}
int Fittarget_correlfun3D_fdf_withlambda (const gsl_vector* xvec_ptr, void *params_ptr, gsl_vector* f_ptr, gsl_matrix* Jacobian_ptr)
{
  Fittarget_correlfun3D_f_withlambda(xvec_ptr, params_ptr, f_ptr);
  Fittarget_correlfun3D_df_withlambda(xvec_ptr, params_ptr, Jacobian_ptr);

  return GSL_SUCCESS;
}

//*********************************************************************
//  Function to return the i'th best-fit parameter
inline double HBT::get_fit_results(int i, gsl_multifit_fdfsolver * solver_ptr)
{
  return gsl_vector_get (solver_ptr->x, i);
}

//*********************************************************************
//  Function to retrieve the square root of the diagonal elements of
//   the covariance matrix.
inline double HBT::get_fit_err (int i, gsl_matrix * covariance_ptr)
{
  return sqrt (gsl_matrix_get (covariance_ptr, i, i));
}
