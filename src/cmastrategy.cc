/**
 * CMA-ES, Covariance Matrix Adaptation Evolution Strategy
 * Copyright (c) 2014 Inria
 * Author: Emmanuel Benazera <emmanuel.benazera@lri.fr>
 *
 * This file is part of libcmaes.
 *
 * libcmaes is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libcmaes is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libcmaes.  If not, see <http://www.gnu.org/licenses/>.
 */

//#define NDEBUG 1

#include "libcmaes_config.h"
#include "cmastrategy.h"
#include "opti_err.h"
#include "llogging.h"
#include <iostream>
#include <chrono>

namespace libcmaes
{

  template <class TGenoPheno> using eostrat = ESOStrategy<CMAParameters<TGenoPheno>,CMASolutions,CMAStopCriteria<TGenoPheno> >;
  
  template <class TCovarianceUpdate, class TGenoPheno>
  ProgressFunc<CMAParameters<TGenoPheno>,CMASolutions> CMAStrategy<TCovarianceUpdate,TGenoPheno>::_defaultPFunc = [](const CMAParameters<TGenoPheno> &cmaparams, const CMASolutions &cmasols)
  {
    LOG_IF(INFO,!cmaparams._quiet) << "iter=" << cmasols._niter << " / evals=" << cmaparams._lambda * cmasols._niter << " / f-value=" << cmasols._best_candidates_hist.back()._fvalue <<  " / sigma=" << cmasols._sigma << (cmaparams._lazy_update && cmasols._updated_eigen ? " / cupdate="+std::to_string(cmasols._updated_eigen) : "") << " / last_iter=" << cmasols._elapsed_last_iter << std::endl;
    return 0;
  };
  
  template <class TCovarianceUpdate, class TGenoPheno>
  CMAStrategy<TCovarianceUpdate,TGenoPheno>::CMAStrategy(FitFunc &func,
					      CMAParameters<TGenoPheno> &parameters)
    :ESOStrategy<CMAParameters<TGenoPheno>,CMASolutions,CMAStopCriteria<TGenoPheno> >(func,parameters)
  {
    eostrat<TGenoPheno>::_pfunc = [](const CMAParameters<TGenoPheno> &cmaparams, const CMASolutions &cmasols)
      {
	LOG_IF(INFO,!cmaparams._quiet) << "iter=" << cmasols._niter << " / evals=" << cmaparams._lambda * cmasols._niter << " / f-value=" << cmasols._best_candidates_hist.back()._fvalue <<  " / sigma=" << cmasols._sigma << (cmaparams._lazy_update && cmasols._updated_eigen ? " / cupdate="+std::to_string(cmasols._updated_eigen) : "") << " " << cmasols._elapsed_last_iter << std::endl;
	return 0;
      };
    _esolver = EigenMultivariateNormal<double>(false,eostrat<TGenoPheno>::_parameters._seed); // seeding the multivariate normal generator.
    LOG_IF(INFO,!eostrat<TGenoPheno>::_parameters._quiet) << "CMA-ES / dim=" << eostrat<TGenoPheno>::_parameters._dim << " / lambda=" << eostrat<TGenoPheno>::_parameters._lambda << " / sigma0=" << eostrat<TGenoPheno>::_solutions._sigma << " / mu=" << eostrat<TGenoPheno>::_parameters._mu << " / mueff=" << eostrat<TGenoPheno>::_parameters._muw << " / c1=" << eostrat<TGenoPheno>::_parameters._c1 << " / cmu=" << eostrat<TGenoPheno>::_parameters._cmu << " / lazy_update=" << eostrat<TGenoPheno>::_parameters._lazy_update << std::endl;
    if (!eostrat<TGenoPheno>::_parameters._fplot.empty())
      _fplotstream.open(eostrat<TGenoPheno>::_parameters._fplot);
  }

  template <class TCovarianceUpdate, class TGenoPheno>
  CMAStrategy<TCovarianceUpdate,TGenoPheno>::~CMAStrategy()
  {
    if (!eostrat<TGenoPheno>::_parameters._fplot.empty())
      _fplotstream.close();
  }
  
  template <class TCovarianceUpdate, class TGenoPheno>
  dMat CMAStrategy<TCovarianceUpdate,TGenoPheno>::ask()
  {
#ifdef HAVE_DEBUG
    std::chrono::time_point<std::chrono::system_clock> tstart = std::chrono::system_clock::now();
#endif
    
    // compute eigenvalues and eigenvectors.
    if (!eostrat<TGenoPheno>::_parameters._sep)
      {
	eostrat<TGenoPheno>::_solutions._updated_eigen = false;
	if (eostrat<TGenoPheno>::_niter == 0 || !eostrat<TGenoPheno>::_parameters._lazy_update
	    || eostrat<TGenoPheno>::_niter - eostrat<TGenoPheno>::_solutions._eigeniter > eostrat<TGenoPheno>::_parameters._lazy_value)
	  {
	    eostrat<TGenoPheno>::_solutions._eigeniter = eostrat<TGenoPheno>::_niter;
	    _esolver.setMean(eostrat<TGenoPheno>::_solutions._xmean);
	    _esolver.setCovar(eostrat<TGenoPheno>::_solutions._cov);
	    eostrat<TGenoPheno>::_solutions._updated_eigen = true;
	  }
      }
    else
      {
	_esolver.setMean(eostrat<TGenoPheno>::_solutions._xmean);
	_esolver._covar = eostrat<TGenoPheno>::_solutions._cov.diagonal().asDiagonal();
	_esolver._transform = eostrat<TGenoPheno>::_solutions._cov.diagonal().asDiagonal();
      }

    //debug
    //std::cout << "transform: " << _esolver._transform << std::endl;
    //debug
    
    // sample for multivariate normal distribution.
    dMat pop = _esolver.samples(eostrat<TGenoPheno>::_parameters._lambda,eostrat<TGenoPheno>::_solutions._sigma); // Eq (1).

    // if some parameters are fixed, reset them.
    if (!eostrat<TGenoPheno>::_parameters._fixed_p.empty())
      {
	for (auto it=eostrat<TGenoPheno>::_parameters._fixed_p.begin();
	     it!=eostrat<TGenoPheno>::_parameters._fixed_p.end();++it)
	  {
	    pop.block((*it).first,0,1,pop.cols()) = dVec::Constant(pop.cols(),(*it).second).transpose();
	  }
      }
    
    //debug
    /*DLOG(INFO) << "ask: produced " << pop.cols() << " candidates\n";
      std::cerr << pop << std::endl;*/
    //debug

#ifdef HAVE_DEBUG
    std::chrono::time_point<std::chrono::system_clock> tstop = std::chrono::system_clock::now();
    eostrat<TGenoPheno>::_solutions._elapsed_ask = std::chrono::duration_cast<std::chrono::milliseconds>(tstop-tstart).count();
#endif
    
    return pop;
  }
  
  template <class TCovarianceUpdate, class TGenoPheno>
  void CMAStrategy<TCovarianceUpdate,TGenoPheno>::tell()
  {
    //debug
    //DLOG(INFO) << "tell()\n";
    //debug

#ifdef DEBUG
    std::chrono::time_point<std::chrono::system_clock> tstart = std::chrono::system_clock::now();
#endif
    
    // sort candidates.
    eostrat<TGenoPheno>::_solutions.sort_candidates();

    // update function value history, as needed.
    eostrat<TGenoPheno>::_solutions.update_best_candidates();
    
    // CMA-ES update, depends on the selected 'flavor'.
    TCovarianceUpdate::update(eostrat<TGenoPheno>::_parameters,_esolver,eostrat<TGenoPheno>::_solutions);
    
    // other stuff.
    if (!eostrat<TGenoPheno>::_parameters._sep)
      eostrat<TGenoPheno>::_solutions.update_eigenv(_esolver._eigenSolver.eigenvalues(),
						    _esolver._eigenSolver.eigenvectors());
    else eostrat<TGenoPheno>::_solutions.update_eigenv(eostrat<TGenoPheno>::_solutions._cov.diagonal(),
						       dMat::Constant(eostrat<TGenoPheno>::_parameters._dim,
								      eostrat<TGenoPheno>::_parameters._dim,1.0));
    eostrat<TGenoPheno>::_solutions._niter = eostrat<TGenoPheno>::_niter;

#ifdef DEBUG
    std::chrono::time_point<std::chrono::system_clock> tstop = std::chrono::system_clock::now();
    eostrat<TGenoPheno>::_solutions._elapsed_tell = std::chrono::duration_cast<std::chrono::milliseconds>(tstop-tstart).count();
#endif
  }

  template <class TCovarianceUpdate, class TGenoPheno>
  bool CMAStrategy<TCovarianceUpdate,TGenoPheno>::stop()
  {
    if (eostrat<TGenoPheno>::_solutions._run_status < 0) // an error occured, most likely out of memory at cov matrix creation.
      return true;
    
    if (eostrat<TGenoPheno>::_niter == 0)
      return false;
    
    if (eostrat<TGenoPheno>::_pfunc(eostrat<TGenoPheno>::_parameters,eostrat<TGenoPheno>::_solutions)) // progress function.
      return true; // end on progress function internal termination, possibly custom.
    
    if (!eostrat<TGenoPheno>::_parameters._fplot.empty())
      plot();
    
    if ((eostrat<TGenoPheno>::_solutions._run_status = _stopcriteria.stop(eostrat<TGenoPheno>::_parameters,eostrat<TGenoPheno>::_solutions)) != 0)
      return true;
    else return false;
  }

  template <class TCovarianceUpdate, class TGenoPheno>
  int CMAStrategy<TCovarianceUpdate,TGenoPheno>::optimize()
  {
    //debug
    //DLOG(INFO) << "optimize()\n";
    //debug
    std::chrono::time_point<std::chrono::system_clock> tstart = std::chrono::system_clock::now();
    while(!stop())
      {
	dMat candidates = ask();
	this->eval(candidates,eostrat<TGenoPheno>::_parameters._gp.pheno(candidates));
	for (int r=0;r<candidates.cols();r++)
	  eostrat<TGenoPheno>::_solutions._candidates.at(r)._x = candidates.col(r);
	tell();
	eostrat<TGenoPheno>::_niter++;
	std::chrono::time_point<std::chrono::system_clock> tstop = std::chrono::system_clock::now();
	eostrat<TGenoPheno>::_solutions._elapsed_last_iter = std::chrono::duration_cast<std::chrono::milliseconds>(tstop-tstart).count();
	tstart = std::chrono::system_clock::now();
      }
    if (eostrat<TGenoPheno>::_solutions._run_status >= 0)
      return OPTI_SUCCESS;
    else return OPTI_ERR_TERMINATION; // exact termination code is in eostrat<TGenoPheno>::_solutions._run_status.
  }

  template <class TCovarianceUpdate, class TGenoPheno>
  void CMAStrategy<TCovarianceUpdate,TGenoPheno>::plot()
  {
    static std::string sep = " ";
    _fplotstream << fabs(eostrat<TGenoPheno>::_solutions._best_candidates_hist.back()._fvalue) << sep
		 << eostrat<TGenoPheno>::_nevals << sep << eostrat<TGenoPheno>::_solutions._sigma << sep << sqrt(eostrat<TGenoPheno>::_solutions._max_eigenv/eostrat<TGenoPheno>::_solutions._min_eigenv) << sep;
    _fplotstream << eostrat<TGenoPheno>::_solutions._leigenvalues.transpose() << sep;
    _fplotstream << eostrat<TGenoPheno>::_solutions._cov.sqrt().diagonal().transpose() << sep; // max deviation in all main axes
    _fplotstream << eostrat<TGenoPheno>::_solutions._xmean.transpose();
    _fplotstream << sep << eostrat<TGenoPheno>::_solutions._elapsed_last_iter;
#ifdef HAVE_DEBUG
    _fplotstream << sep << eostrat<TGenoPheno>::_solutions._elapsed_eval << sep << eostrat<TGenoPheno>::_solutions._elapsed_ask << sep << eostrat<TGenoPheno>::_solutions._elapsed_tell << sep << eostrat<TGenoPheno>::_solutions._elapsed_stop;
#endif
    _fplotstream << std::endl;
  }
  
  template class CMAStrategy<CovarianceUpdate,GenoPheno<NoBoundStrategy>>;
  template class CMAStrategy<ACovarianceUpdate,GenoPheno<NoBoundStrategy>>;
  template class CMAStrategy<CovarianceUpdate,GenoPheno<pwqBoundStrategy>>;
  template class CMAStrategy<ACovarianceUpdate,GenoPheno<pwqBoundStrategy>>;
  template class CMAStrategy<CovarianceUpdate,GenoPheno<NoBoundStrategy,linScalingStrategy>>;
  template class CMAStrategy<ACovarianceUpdate,GenoPheno<NoBoundStrategy,linScalingStrategy>>;
  template class CMAStrategy<CovarianceUpdate,GenoPheno<pwqBoundStrategy,linScalingStrategy>>;
  template class CMAStrategy<ACovarianceUpdate,GenoPheno<pwqBoundStrategy,linScalingStrategy>>;
}
