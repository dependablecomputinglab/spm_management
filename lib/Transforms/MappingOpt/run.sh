LLVM_DIR=/home/ybkim/workspace/spm/code_management/smm_llvm

cp ./* $LLVM_DIR/build/lib/Transforms/MappingOpt
cd $LLVM_DIR/build/lib/Transforms/MappingOpt
make -j8
