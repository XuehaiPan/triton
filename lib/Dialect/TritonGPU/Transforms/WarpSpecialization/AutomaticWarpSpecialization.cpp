#include "mlir/Dialect/Arith/Transforms/Passes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "triton/Dialect/TritonGPU/Transforms/Passes.h"
#include "triton/Dialect/TritonGPU/Transforms/Utility.h"

using namespace mlir;
using namespace triton;
using namespace triton::gpu;
namespace ttng = triton::nvidia_gpu;

//===----------------------------------------------------------------------===//
// Pass Definition
//===----------------------------------------------------------------------===//

namespace mlir::triton::gpu {
#define GEN_PASS_DEF_TRITONGPUAUTOMATICWARPSPECIALIZATION
#include "triton/Dialect/TritonGPU/Transforms/Passes.h.inc"
} // namespace mlir::triton::gpu

namespace {
struct AutomaticWarpSpecialization
    : triton::gpu::impl::TritonGPUAutomaticWarpSpecializationBase<
          AutomaticWarpSpecialization> {
  using TritonGPUAutomaticWarpSpecializationBase::
      TritonGPUAutomaticWarpSpecializationBase;

  void runOnOperation() override;
};
} // namespace

void AutomaticWarpSpecialization::runOnOperation() {
  OpPassManager pm;
  pm.addPass(createTritonGPULoadMMASpecialization({numStages}));
  pm.addPass(createTritonGPURewritePartitionDependencies());
  // `int-range-optimizations` combines SCCP with integer range analysis. It's
  // good at cleaning up loop arithmetic.
  pm.addPass(arith::createIntRangeOptimizationsPass());
  pm.addPass(createCSEPass());
  pm.addPass(createTritonGPUPartitionLoops());
  if (failed(runPipeline(pm, getOperation())))
    return signalPassFailure();

  // Cleanup code generated by warp specialization.
  RewritePatternSet patterns(&getContext());
  populateForOpDeadArgumentElimination(patterns);
  scf::ForOp::getCanonicalizationPatterns(patterns, &getContext());
  scf::IfOp::getCanonicalizationPatterns(patterns, &getContext());
  WarpSpecializeOp::getCanonicalizationPatterns(patterns, &getContext());
  if (failed(applyPatternsGreedily(getOperation(), std::move(patterns))))
    return signalPassFailure();

  pm.clear();
  pm.addPass(createTritonGPUOptimizePartitionWarps());
  if (failed(runPipeline(pm, getOperation())))
    return signalPassFailure();
}
