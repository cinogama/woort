# IR

由于 WooRT 的字节码需要考虑操作数范围、跳转范围等一系列制约因素，我们希望有一个类似 AsmJit/LLVM 那样的，有无限虚拟寄存器的 IR 接口，开发者通过调用这些 IR API，生成最终的 WooRT 字节码

## 概念

* CodeEnv：也就是 `woort_CodeEnv`, 概念上类似 Module，包含一系列函数的字节码和它们所需的常量/静态存储区域。
* ByteCode：字节码，由 `woort_VMRuntime` 直接执行的指令码，大部分字节码都是 4 字节的，有一部分字节码指令带有额外的 4 字节拓展操作数。