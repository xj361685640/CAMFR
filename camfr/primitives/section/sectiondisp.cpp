
/////////////////////////////////////////////////////////////////////////////
//
// File:     sectiondisp.cpp
// Author:   Peter.Bienstman@rug.ac.be
// Date:     20020125
// Version:  1.0
//
// Copyright (C) 2002 Peter Bienstman - Ghent University
//
/////////////////////////////////////////////////////////////////////////////

//#include "arscomp.h"
#include "section.h"
#include "sectiondisp.h"
#include "../slab/generalslab.h"
#include "../slab/slabmatrixcache.h"
#include "../../math/linalg/linalg.h"

using std::vector;
using std::cout;
using std::cerr;
using std::endl;

#include "../../util/vectorutil.h"

/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::SectionDisp()
//
/////////////////////////////////////////////////////////////////////////////

SectionDisp::SectionDisp(Stack& _left, Stack& _right, Real _lambda, int _M, 
                         bool sym)
  : left(&_left), right(&_right), lambda(_lambda), M(_M), symmetric(sym) 
{
  // TODO: Rework.

  // Determine left slabs.

  const vector<Chunk>* l 
    = dynamic_cast<StackImpl*>(left->get_flat_sc())->get_chunks();

  for (unsigned int i=0; i<l->size(); i++)
  {
    left_slabs.push_back(dynamic_cast<Slab*>((*l)[i].sc->get_inc()));
    left_slabs.push_back(dynamic_cast<Slab*>((*l)[i].sc->get_ext()));
  }

  remove_copies(&left_slabs);

  // Determine right slabs.

  const vector<Chunk>* r 
    = dynamic_cast<StackImpl*>(right->get_flat_sc())->get_chunks();

  for (unsigned int i=0; i<r->size(); i++)
  {
    right_slabs.push_back(dynamic_cast<Slab*>((*r)[i].sc->get_inc()));
    right_slabs.push_back(dynamic_cast<Slab*>((*r)[i].sc->get_ext()));
  } 

  remove_copies(&right_slabs);

  // Get pointers to top-level (not flat_sc) thicknesses.

  left_d  = dynamic_cast<StackImpl*>( left->get_sc())->get_thicknesses();
  right_d = dynamic_cast<StackImpl*>(right->get_sc())->get_thicknesses();

  // Determine slabs.

  for (int i=l->size()-1; i>=0; i--)
  {
    Complex d_i = (*l)[i].d;

    if (abs(d_i) > 1e-6)
    {
      d.push_back(d_i);
      slabs.push_back(dynamic_cast<Slab*>((*l)[i].sc->get_ext()));
      //std::cout << slabs.back()->get_core()->n() 
      //          << " " << d.back() << std::endl;
    }
  }

  for (int i=0; i<r->size(); i++)
  {
    Complex d_i = (*r)[i].d;

    if (abs(d_i) > 1e-6)
    {
      d.push_back(d_i);    
      slabs.push_back(dynamic_cast<Slab*>((*r)[i].sc->get_ext()));
      //std::cout << slabs.back()->get_core()->n() 
      //          << " " << d.back() << std::endl;
    }
  }

  // Create whole stack.

  Expression e;
  for (unsigned int i=0; i<slabs.size(); i++)
    e += Term((*slabs[i])(d[i]));
  
  stack = new Stack(e);

  // Determine minimum refractive index.

  vector<Material*> materials = left->get_materials();
  vector<Material*> right_mat = right->get_materials();
  materials.insert(materials.end(), right_mat.begin(), right_mat.end());

  min_eps_mu = materials[0]->eps_mu();
  
  for (unsigned int i=1; i<materials.size(); i++)
  {
    Complex eps_mu = materials[i]->eps_mu();
    
    if (real(eps_mu) < real(min_eps_mu))
      min_eps_mu = eps_mu;
  }
}



/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::operator()
//
//   kt = k in the region with minimum refractive index.
//
/////////////////////////////////////////////////////////////////////////////

Complex SectionDisp::operator()(const Complex& kt)
{
  counter++;

  global.lambda = lambda;
  global.polarisation = TE_TM;

  bool old_orthogonal = global.orthogonal;
  global.orthogonal = false;

  Complex old_beta = global.slab_ky;

  const Complex C = pow(2*pi/lambda, 2) / (eps0 * mu0);
  Complex beta = sqrt(C*min_eps_mu - kt*kt);

  if (real(beta) < 0)
    beta = -beta;
  
  if (abs(imag(beta)) < 1e-12)
    if (imag(beta) > 0)
      beta = -beta;

  //std::cout << "beta" << beta << std::endl;

  global.slab_ky = beta;

  int old_N = global.N;
  global.N = M;

  //Complex res = (global.eigen_calc==lapack) ? calc_lapack () : calc_arnoldi();

  Complex res = (global.eigen_calc==lapack) ? calc_lapack () : calc_lapack2();

  global.N = old_N;
  global.slab_ky = old_beta;
  global.orthogonal = old_orthogonal;

  return res;
}



/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::calc_lapack()
//
/////////////////////////////////////////////////////////////////////////////

Complex SectionDisp::calc_lapack3()
{
  // Calculate eigenvectors.

  left->calcRT();
  if (! symmetric)
    right->calcRT();
  
  cMatrix Q(M,M,fortranArray);
  if (! symmetric)
    Q.reference(multiply( left->as_multi()->get_R12(), 
                         right->as_multi()->get_R12()));
  else
    Q.reference(multiply( left->as_multi()->get_R12(), 
                          left->as_multi()->get_R12()));

  for (int i=1; i<=M; i++)
    Q(i,i) -= 1.0;
  
  cVector e(M,fortranArray);
  if (global.stability == normal)
    e.reference(eigenvalues(Q));
  else
    e.reference(eigenvalues_x(Q));
  
  // Return product of eigenvalues-1.

  Complex product = 1.0;
  vector<unsigned int> min_indices;
/*
  for (unsigned int k=0; k<30; k++)
  {
    int min_index = 1;
    Real min_distance = 1e200;

    for (int i=1; i<=M; i++)
      if ( (abs(e(i) - 1.0) < min_distance) &&
        ( find(min_indices.begin(),min_indices.end(),i) == min_indices.end()))
      {
        min_index = i;  
        min_distance = abs(e(i) - 1.0);
      }

    min_indices.push_back(min_index);
    product *= .5*(e(min_index) - 1.0);
  }
*/

  //for (unsigned int k=0; k<min_indices.size(); k++)
  //  std::cout << min_indices[k] << " " << abs(e(min_indices[k]) - 1.0) << std::endl;

  for (int i=1; i<=M; i++)
    product *= .5*(e(i) - 1.0);
  
  return product;
}


Complex SectionDisp::calc_lapack()
{
  // Calculate eigenvectors.

  left->calcRT();
  if (! symmetric)
    right->calcRT();
  
  cMatrix Q(M,M,fortranArray);
  if (! symmetric)
    Q.reference(multiply( left->as_multi()->get_R12(), 
                         right->as_multi()->get_R12()));
  else
    Q.reference(multiply( left->as_multi()->get_R12(), 
                          left->as_multi()->get_R12()));

  // SVD

/*
  cMatrix I(M,M,fortranArray);
  I = 0.0;
  for (int i=1; i<=M; i++)
    I(i,i) = 1.0;

  cMatrix Q2(M,M,fortranArray);  
  Q2 = Q-I;
  
  rVector s(M,fortranArray);
  s = svd(Q2);

  std::cout << s << std::endl;
  
  return s(1);
*/ 
  
  cVector e(M,fortranArray);
  if (global.stability == normal)
    e.reference(eigenvalues(Q));
  else
    e.reference(eigenvalues_x(Q));

  // Return product.
/*  
  Complex product = 1.0;
  
  for (int i=1; i<=M; i++)
    product *= e(i)-1.;

  //std::cout << e << std::endl;
  
  return product;
*/
  
  
  // Return minimum distance of eigenvalues to 1.

  int min_index = 1;

  for (int i=2; i<=M; i++)
    if (abs(e(i) - 1.0) < abs(e(min_index) - 1.0))
      min_index = i;

  return e(min_index) - 1.0;
}



Complex SectionDisp::calc_lapack4()
{
  blitz::Range r1(1,M); blitz::Range r2(M+1,2*M);
  
  // Propagate in first medium.

  Complex f=1.0;

  cMatrix T(2*M,2*M,fortranArray); T = 0.0;

  slabs[0]->find_modes();
  
  Complex kz_max = slabs[0]->get_mode(M)->get_kz();
  f *= exp(I*kz_max);
  for (int i=1; i<=M; i++)
  {
    Complex kz_i = slabs[0]->get_mode(i)->get_kz();
    //Complex kz_max = slabs[0]->get_mode(i)->get_kz();

    T(  i,   i) = exp(I * (-kz_i - kz_max) * d[0]);
    T(M+i, M+i) = exp(I * (+kz_i - kz_max) * d[0]);
  }
  
  // Loop over other slabs.

  for (unsigned int k=1; k<slabs.size(); k++)
  {
    slabs[k]->find_modes();
    
    // Cross interface.

    cMatrix O_I_I (M,M,fortranArray); cMatrix O_II_II(M,M,fortranArray);
    cMatrix O_I_II(M,M,fortranArray); cMatrix O_II_I (M,M,fortranArray);

    slabs[k-1]->calc_overlap_matrices(slabs[k],
                                      &O_I_II, &O_II_I, &O_I_I, &O_II_II);
    
    cMatrix inv_O_II_II(M,M,fortranArray);

    if (global.stability != SVD)
      inv_O_II_II.reference(invert    (O_II_II));
    else
      inv_O_II_II.reference(invert_svd(O_II_II));

    cMatrix P(M,M,fortranArray); 
    P.reference(multiply(inv_O_II_II, O_I_II, transp, transp));

    cMatrix Q(M,M,fortranArray);
    Q.reference(multiply(inv_O_II_II, O_II_I));

    cMatrix T_k(2*M,2*M,fortranArray);    
    T_k(r1,r1) = P+Q;        T_k(r1,r2) = P-Q;
    T_k(r2,r1) = T_k(r1,r2); T_k(r2,r2) = T_k(r1,r1);

    // Propagate.

    cVector prop(2*M,fortranArray);
    Complex kz_max = slabs[k]->get_mode(M)->get_kz();
    f *= exp(I*kz_max);
    for (int i=1; i<=M; i++)
    {
      Complex kz_i = slabs[k]->get_mode(i)->get_kz();
      //Complex kz_max = slabs[k]->get_mode(i)->get_kz();

      prop(  i) = exp(I * (-kz_i - kz_max) * d[k]);
      prop(M+i) = exp(I * (+kz_i - kz_max) * d[k]);
    }

    //std::cout << prop << std::endl;

    for (int i=1; i<=2*M; i++)
      for (int j=1; j<=2*M; j++)
        T_k(i,j) *= 0.5 * prop(i);
    
    // Combine.

    T.reference(multiply(T_k,T));
  }
  
  // Extract submatrices of T and change sign if appropriate.
  // TODO: get rid of multiplications.

  cMatrix A(M,M,fortranArray); cMatrix B(M,M,fortranArray);
  cMatrix C(M,M,fortranArray); cMatrix D(M,M,fortranArray);

  A = T(r1,r1); B = T(r1,r2);
  C = T(r2,r1); D = T(r2,r2);

  Complex R0 = (global_section. leftwall == E_wall) ? -1.0 : 1.0;
  Complex Rn = (global_section.rightwall == E_wall) ? -1.0 : 1.0;
 
  cMatrix Q(M,M,fortranArray); Q = (R0*Rn)*A + Rn*B - R0*C - D;
  
  // Calculate eigenvectors.

  cVector e(M,fortranArray);
  if (global.stability == normal)
    e.reference(eigenvalues(Q));
  else
    e.reference(eigenvalues_x(Q));

  // Return product of eigenvalues.
  
  Complex product = 1.0;
  for (int i=1; i<=M; i++)
    product *= e(i);
  
  return product;

  // Return smallest eigenvalue.

  int min_index = 1;

  for (int i=2; i<=M; i++)
    if (abs(e(i)) < abs(e(min_index)))
      min_index = i;

  return e(min_index);
}


// Version based on S matrix
Complex SectionDisp::calc_lapack2()
{
  // Calculate matrix.

  stack->calcRT();
 
  cMatrix Q(2*M,2*M,fortranArray);

  MultiScatterer* s = stack->as_multi();

  blitz::Range r1(1,M); blitz::Range r2(M+1,2*M);
  
  Q(r1,r1) = (global_section.rightwall==E_wall) ? -s->get_R21() : s->get_R21();
  Q(r1,r2) = (global_section.leftwall ==E_wall) ? -s->get_T12() : s->get_T12();

  Q(r2,r1) = (global_section.rightwall==E_wall) ? -s->get_T21() : s->get_T21();
  Q(r2,r2) = (global_section.leftwall ==E_wall) ? -s->get_R12() : s->get_R12();
  
  // Calculate eigenvectors.

  cVector e(2*M,fortranArray);
  if (global.stability == normal)
    e.reference(eigenvalues(Q));
  else
    e.reference(eigenvalues_x(Q));

  // Return product of eigenvalues.

  Complex product = 1.0;
  for (int i=1; i<=2*M; i++)
    product *= e(i)-1.0;

  //std::cout << e << std::endl;
  //std::cout << "R" << s->get_R21() << std::endl;
  
  return product;

  // Return eigenvalue closest to 1.

  int min_index = 1;

  for (int i=2; i<=2*M; i++)
    if (abs(e(i)-1.0) < abs(e(min_index)-1.0))
      min_index = i;

  return e(min_index)-1.0;
}



/////////////////////////////////////////////////////////////////////////////
//
// Multiplier
//
//   Auxiliary class for efficient calculation of the product of a vector
//   with the cavity matrix.
//
/////////////////////////////////////////////////////////////////////////////

class Multiplier
{
  public:

    Multiplier(const cMatrix& L_, const cMatrix& R_) 
      : L(&L_), R(&R_), Q(NULL), counter(0) {}

    ~Multiplier() {delete Q;}
   
    void mult(Complex* in, Complex* out) 
    {
      counter++;
      
      cVector i(in,  global.N, blitz::neverDeleteData, fortranArray);
      cVector o(out, global.N, blitz::neverDeleteData, fortranArray); 

      if (counter == global.N)
      {
        Q = new cMatrix(global.N, global.N, fortranArray);
        Q->reference(multiply(*L, *R));
        for (int k=1; k<=global.N; k++)
          (*Q)(k,k) -= 1.0;
      }

      if (counter < global.N)
        o = multiply(*L, *R, i) - i;
      else
        o = multiply(*Q, i);
    }

  protected:

    int counter;
    
    const cMatrix* L;
    const cMatrix* R;

    cMatrix* Q;
};



/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::calc_arnoldi()
//
/////////////////////////////////////////////////////////////////////////////

Complex SectionDisp::calc_arnoldi()
{
/*
  left->calcRT();
  if (! symmetric)
    right->calcRT();

  Multiplier m (              left->as_multi()->get_R12(),
                 symmetric ?  left->as_multi()->get_R12()
                           : right->as_multi()->get_R12());

  int nev = 1;
  int ncv = 6;
  double tol = 1e-3;
  int max_iter = 1000;

  ARCompStdEig<Real, Multiplier> prob
    (M, nev, &m, &Multiplier::mult, "SM", ncv, tol, max_iter);

  prob.FindEigenvalues();

  if (prob.ConvergedEigenvalues() == 0)
  {
    cout << "Warning: Arnoldi solver did not converge for beta "
         << beta << endl << "Using LAPACK instead. " << endl;
    
    return calc_lapack(beta);
  }

  return prob.Eigenvalue(0);
*/
}



/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::get_params()
//
///////////////////////////////////////////////////////////////////////////// 

vector<Complex> SectionDisp::get_params() const
{
  vector<Complex> params;

  for (unsigned int i=0; i<left_slabs.size(); i++)
  {
    vector<Complex> params_i = left_slabs[i]->get_params();
    params.push_back(params_i.size());
    params.insert(params.end(), params_i.begin(), params_i.end());
  }

  for (unsigned int i=0; i<right_slabs.size(); i++)
  {
    vector<Complex> params_i = right_slabs[i]->get_params();
    params.push_back(params_i.size());
    params.insert(params.end(), params_i.begin(), params_i.end());
  }

  for (unsigned int i=0; i<left_d.size(); i++)
    params.push_back(*left_d[i]);

  for (unsigned int i=0; i<right_d.size(); i++)
    params.push_back(*right_d[i]);

  params.push_back(lambda);

  params.push_back(min_eps_mu);
  
  return params;
}



/////////////////////////////////////////////////////////////////////////////
//
// SectionDisp::set_params()
//
/////////////////////////////////////////////////////////////////////////////

void SectionDisp::set_params(const vector<Complex>& params)
{
  unsigned int params_index = 0;

  for (unsigned int i=0; i<left_slabs.size(); i++)
  {
    unsigned int j_max = (unsigned int)(real(params[params_index++]));

    vector<Complex> params_i;
    for (unsigned int j=0; j<j_max; j++)
      params_i.push_back(params[params_index++]);
    
    left_slabs[i]->set_params(params_i);
  }

  for (unsigned int i=0; i<right_slabs.size(); i++)
  {
    unsigned int j_max = (unsigned int)(real(params[params_index++]));

    vector<Complex> params_i;
    for (unsigned int j=0; j<j_max; j++)
      params_i.push_back(params[params_index++]);
    
    right_slabs[i]->set_params(params_i);
  }

  for (unsigned int i=0; i<left_d.size(); i++)
    *left_d[i]  = params[params_index++];

  for (unsigned int i=0; i<right_d.size(); i++)
    *right_d[i] = params[params_index++];
  
  lambda = real(params[params_index++]);

  min_eps_mu = params[params_index];

   left->freeRT();
  right->freeRT();
}
