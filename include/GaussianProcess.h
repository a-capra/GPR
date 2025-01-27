/*
 * Copyright 2015 Christoph Jud (christoph.jud@unibas.ch)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

#include <Eigen/Dense>

#include "Kernel.h"

namespace gpr{
template <class TScalarType> class Likelihood;

template< class TScalarType>
class GaussianProcess
{
public:
	typedef GaussianProcess Self;
    typedef std::shared_ptr<Self> Pointer;
	typedef Kernel<TScalarType> KernelType;
    typedef typename KernelType::Pointer KernelTypePointer;

    typedef Eigen::Matrix<TScalarType, Eigen::Dynamic, 1> VectorType;
    typedef Eigen::Matrix<TScalarType, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> MatrixType;
    typedef Eigen::DiagonalMatrix<TScalarType, Eigen::Dynamic> DiagMatrixType;

	typedef std::vector<VectorType> VectorListType;
	typedef std::vector<MatrixType> MatrixListType;

    typedef long double HighPrecisionType;


    /*
     * In the kernel regression method, the kernel matrix has to be inverted. There
     * are several methods available in the Eigen3 library to invert a matrix. The
     * standard way to invert a matrix is by the LU decomposition with full pivoting.
     * it is very fast, but however might by instable. That means, that the inversion
     * inaccuracy leads to a Gaussian process kernel which is negative definite.
     * Therefore, with the method SetInversionMethod one can switch between other
     * inversion methods. These are:
     *
     *  - JacobiSVD:    this method is very accurate but for large problems too slow.
     *  - BDCSVD:       this method is accurate as well, but faster than JacobiSVD.
     *                  However, compared to FullPivotLU, it is slow as well.
     *  - SelfAdjointEigenSolver: this method is optimized for symmetric matrices.
     *                  Good for medium sized problems.
     */
    typedef enum { FullPivotLU=0, JacobiSVD=1, BDCSVD=2, SelfAdjointEigenSolver=3 } InversionMethod;
	
	/*
	 * Add a new sample lable pair to the gaussian process
	 *  x is the input vector
	 *  y the corresponding label vector
	 */
    void AddSample(const VectorType &x, const VectorType &y);

	/*
	 * Predict new data point
	 */
    virtual VectorType Predict(const VectorType &x);

	/*
	 * Predict new point (return value) and its derivative input parameter D
	 */
    virtual VectorType PredictDerivative(const VectorType &x, MatrixType &D);


    /*
     * Returns the scalar product between x and y in the RKHS of this GP
     */
    virtual TScalarType operator()(const VectorType & x, const VectorType & y);

    /*
     * Returns the positive credible interval at point x
     */
    TScalarType GetCredibleInterval(const VectorType&x);
	
	/*
	 * If sample data has changed perform learning step
	 */
    virtual void Initialize();


    // Constructors
    GaussianProcess(KernelTypePointer kernel) : m_Sigma(0),
                                                m_Initialized(false),
                                                m_InputDimension(0),
                                                m_OutputDimension(0),
                                                m_InvMethod(FullPivotLU),
                                                m_EfficientStorage(false),
                                                debug(false) {
        m_Kernel = kernel;
	}

    virtual ~GaussianProcess() {
        if(debug) std::cout << "GaussianProcess::~GaussianProcess() destruct object" << std::endl;
    }

    const KernelTypePointer GetKernel() { return m_Kernel; }
    void SetKernel(KernelTypePointer k) {
        m_Kernel = k;
        m_Initialized = false;
    }

    // Some get / set methods
    void DebugOn(){
		debug = true;
	}

    virtual unsigned GetNumberOfSamples() const{
		return m_SampleVectors.size();
	}

    TScalarType GetSigma() const{
        return m_Sigma;
    }

    TScalarType GetSigmaSquared() const{
        return m_Sigma*m_Sigma;
    }

    void SetSigma(TScalarType sigma){
        m_Sigma = sigma;
        m_Initialized = false;
    }

    virtual unsigned GetNumberOfInputDimensions() const{ return m_InputDimension; }

    virtual void SetInversionMethod(InversionMethod m){ m_InvMethod = m; }
    virtual InversionMethod GetInversionMethod(){ return m_InvMethod; }

    // Since for the kernel matrix a lot of memory might be needed,
    // one can turn on the efficient storage mode, which stores only
    // the regression vectors. However keep in mind that, if the core matrix has to be
    // recalculated later, there is a numerical difference between
    // the matrices.
    bool GetEfficientStorage(){ return m_EfficientStorage; }
    void SetEfficientStorage(bool s){ m_EfficientStorage = s; }

    // IO methods
    virtual void Save(std::string prefix);
    virtual void Load(std::string prefix);

    virtual void ToString() const;

    void Lock(){ gp_lock.lock(); }
    void UnLock(){ gp_lock.unlock(); }

    // Comparison operator
    virtual bool operator ==(const GaussianProcess<TScalarType> &b) const;
    virtual bool operator !=(const GaussianProcess<TScalarType> &b) const{
        return ! operator ==(b);
    }

protected:

	/*
	 * Computation of kernel matrix K_ij = k(x_i, x_j)
	 * 	- it is symmetric therefore only half of the kernel evaluations
	 * 	  has to be performed
     *
     * (The actual computation is performed in ComputeKernelMatrixInternal)
	 */
    virtual void ComputeKernelMatrix(MatrixType &M) const;

    /*
     * Adds m_Sigma*m_Sigma to each K_ii
     */
    virtual void AddNoiseToKernelMatrix(MatrixType &M) const;


    /*
     * Returns the trace of the kernel matrix
     */
    virtual TScalarType ComputeKernelMatrixTrace() const;

    /*
     * Returns the trace of the derivative kernel matrix
     */
    virtual VectorType ComputeDerivativeKernelMatrixTrace() const;


    /*
     * Computation of the derivative kernel matrix D_i = delta K / delta params_i
     * 	- returns a matrix: [D_0
     *                        .
     *                       D_i
     *                        .
     *                       D_m-1]
     *    for m = number of params and D_i in nxn, n = number of samples
     *
     * (calls ComputeDerivativeKernelMatrixInternal)
     */
    virtual void ComputeDerivativeKernelMatrix(MatrixType &M) const;

    /*
     * Computation of the derivative kernel matrix
     */
    virtual void ComputeDerivativeKernelMatrixInternal(MatrixType &M, const VectorListType& samples) const;

    /*
     * Computation of the core matrix inv(K + sigma2 I)
     * it returns the determinant of K + sigma2 I just in case if it is needed
     */
     void ComputeCoreMatrix(MatrixType &C) const;

     /*
      * Same as ComputeCoreMatrix but returns determinant of the kernel matrix
      */
     HighPrecisionType ComputeCoreMatrixWithDeterminant(MatrixType &C) const;

    /*
     * Inversion of the kernel matrix. TODO: should go into a base class
     */
    virtual MatrixType InvertKernelMatrix(const MatrixType &K, InversionMethod inv_method = FullPivotLU, bool stable=false) const;

	/*
	 * Bring the label vectors in a matrix form Y,
	 * where the rows are the labels.
     *
     * (it is actually performed in ComputeLabelMatrixInternal)
	 */
    virtual void ComputeLabelMatrix(MatrixType &Y) const;

	/*
	 * Lerning is performed.
	 */
    virtual void ComputeRegressionVectors();

	/*
	 * Computation of the kernel vector V_i = k(x, x_i)
     *
     * (calls ComputeKernelVectorInternal)
	 */
    virtual void ComputeKernelVector(const VectorType &x, VectorType &Kx) const;

	/*
	 * Compute difference matrix X = [x-x_0, x-x_1, ... x-x_n]^T
	 */
    void ComputeDifferenceMatrix(const VectorType &x, MatrixType &X) const;

	/*
	 * Assertion functions to check input and output dimensions of the vectors
	 */
    void CheckInputDimension(const VectorType &x, std::string msg_prefix) const;
    void CheckOutputDimension(const VectorType &y, std::string msg_prefix) const;


    KernelTypePointer m_Kernel; // pointer to kernel
	
    TScalarType m_Sigma; // noise on sample data

	VectorListType m_SampleVectors;  // Dimensionality: TInputDimension
	VectorListType m_LabelVectors;   // Dimensionality: TOutputDimension
	MatrixType m_RegressionVectors; // for each output dimension there is one regression vector
    MatrixType m_CoreMatrix;        // is only compared in the == operator if both have m_EfficientStorage set to false
	
	bool m_Initialized;
	unsigned m_InputDimension;
	unsigned m_OutputDimension;
    InversionMethod m_InvMethod; // is not saved/loaded
    bool m_EfficientStorage;

    std::mutex gp_lock;

	bool debug;


//TODO: all internal methods should go into a base class

    /*
     * Computation of kernel matrix K_ij = k(x_i, x_j)
     *  - the matrix M will be resized to the required size (number of samples)
     * 	- it is symmetric therefore only half of the kernel evaluations
     * 	  has to be performed
     *  - the kernel matrix is computed using the provided samples
     */
    void ComputeKernelMatrixInternal(MatrixType &M, const VectorListType& samples) const;

    /*
     * Returns the trace of the kernel matrix
     *  sum(k(x_i, x_i)
     */
    TScalarType ComputeKernelMatrixTraceInternal(const VectorListType& samples) const;

    /*
     * Returns the trace of the derivative kernel matrix
     *  partial / partial theta sum(k(x_i, x_i))
     */
    VectorType ComputeDerivativeKernelMatrixTraceInternal(const VectorListType& samples) const;

    /*
     * Bring the label vectors in a matrix form Y,
     * where the rows are the labels.
     */
    void ComputeLabelMatrixInternal(MatrixType &Y, const VectorListType& labels) const;


    /*
     * Computation of the kernel vector V_i = k(x, x_i)
     */
    void ComputeKernelVectorInternal(const VectorType &x, VectorType &Kx, const VectorListType& samples) const;

private:

	GaussianProcess(const Self &); //purposely not implemented
	void operator=(const Self &); //purposely not implemented

    friend class Likelihood<TScalarType>;
};

}
#include "Likelihood.h"
