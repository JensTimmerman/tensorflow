/* Copyright 2015 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// See docs in ../ops/nn_ops.cc.

#define EIGEN_USE_THREADS

#include "tensorflow/core/framework/op_kernel.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"
#include "tensorflow/core/kernels/sparse_xent_op.h"
#include "tensorflow/core/public/tensor.h"
#include "tensorflow/core/public/tensor_shape.h"

namespace tensorflow {

typedef Eigen::ThreadPoolDevice CPUDevice;
typedef Eigen::GpuDevice GPUDevice;

template <typename Device, typename T>
class SparseSoftmaxXentWithLogitsOp : public OpKernel {
 public:
  explicit SparseSoftmaxXentWithLogitsOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {
    const Tensor& logits_in = context->input(0);
    const Tensor& labels_in = context->input(1);
    OP_REQUIRES(context, logits_in.shape().dim_size(0) == labels_in.NumElements(),
                errors::InvalidArgument(
                    "logits first dimension must match labels size.  logits shape=",
                    logits_in.shape().DebugString(), " labels shape=",
                    labels_in.shape().DebugString()));
    OP_REQUIRES(context, TensorShapeUtils::IsMatrix(logits_in.shape()),
                errors::InvalidArgument("logits must be 2-dimensional"));
    // As we already tested that both inputs have the same shape no need to
    // check that "labels" is a matrix too.

    // loss is 1-D (one per example), and size is batch_size.

    Tensor scratch;
    OP_REQUIRES_OK(
        context, context->allocate_temp(DataTypeToEnum<T>::value,
                                        TensorShape({logits_in.dim_size(0)}),
                                        &scratch));

    Tensor* loss_out = nullptr;
    OP_REQUIRES_OK(context,
                   context->allocate_output(
                       0, TensorShape({logits_in.dim_size(0)}), &loss_out));
    Tensor* back_out = nullptr;
    OP_REQUIRES_OK(context,
                   context->allocate_output(1, logits_in.shape(), &back_out));

    functor::SparseXentFunctor<Device, T> functor;
    functor(context->eigen_device<Device>(), logits_in.matrix<T>(),
            labels_in.vec<int64>(), scratch.vec<T>(), loss_out->vec<T>(),
            back_out->matrix<T>());
  }
};

// Partial specialization for a CPUDevice, that uses the Eigen implementation
// from XentEigenImpl.
namespace functor {
template <typename T>
struct SparseXentFunctor<CPUDevice, T> {
  void operator()(const CPUDevice& d, typename TTypes<T>::ConstMatrix logits,
                  typename TTypes<int64>::ConstVec labels,
                  typename TTypes<T>::Vec scratch,
                  typename TTypes<T>::Vec loss,
                  typename TTypes<T>::Matrix backprop) {
    SparseXentEigenImpl<CPUDevice, T>::Compute(d, logits, labels, scratch, loss,
                                         backprop);
  }
};
}  // namespace functor

REGISTER_KERNEL_BUILDER(Name("SparseSoftmaxCrossEntropyWithLogits")
                            .Device(DEVICE_CPU)
                            .TypeConstraint<float>("T"),
                        SparseSoftmaxXentWithLogitsOp<CPUDevice, float>);
REGISTER_KERNEL_BUILDER(Name("SparseSoftmaxCrossEntropyWithLogits")
                            .Device(DEVICE_CPU)
                            .TypeConstraint<double>("T"),
                        SparseSoftmaxXentWithLogitsOp<CPUDevice, double>);

#if GOOGLE_CUDA
REGISTER_KERNEL_BUILDER(Name("SparseSoftmaxCrossEntropyWithLogits")
                            .Device(DEVICE_GPU)
                            .TypeConstraint<float>("T"),
                        SparseSoftmaxXentWithLogitsOp<GPUDevice, float>);
#endif  // GOOGLE_CUDA

}  // namespace tensorflow
