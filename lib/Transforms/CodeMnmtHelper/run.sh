LLVM_DIR=/home/ybkim/workspace/spm/code_management/smm_llvm

cp ./* $LLVM_DIR/build/lib/Transforms/CodeMnmtHelper
cd $LLVM_DIR/build/lib/Transforms/CodeMnmtHelper
make
