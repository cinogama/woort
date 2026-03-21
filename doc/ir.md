# IR

由于 WooRT 的字节码需要考虑操作数范围、跳转范围等一系列制约因素，我们希望有一个类似 AsmJit/LLVM 那样的，有无限虚拟寄存器的 IR 接口，开发者通过调用这些 IR API，生成最终的 WooRT 字节码

## 概念

* CodeEnv：也就是 `woort_CodeEnv`, 概念上类似 Module，包含一系列函数的字节码和它们所需的常量/静态存储区域。
* ByteCode：字节码，由 `woort_VMRuntime` 直接执行的指令码，大部分字节码都是 4 字节的，有一部分字节码指令带有额外的 4 字节拓展操作数。
* IR：结构上类似字节码，但是编写时不考虑操作数的寻址/跳转范围，由字节码构建流程负责整理和优化最终代码生成

## CodeEnv

CodeEnv 负责持有字节码和常量/静态静态储存区域，一个 CodeEnv 可以包含若干个函数，例如：

```c
    const woort_Bytecode bcs[] =
    {
        // Function: fib(n: int)=> int
        /*
        PUSHRCHK    3                           ; Reserving stack.
        LOAD        [SB - 0] = G[0]             ; Load 2
        LOAD        [SB - 1] = G[1]             ; Load 1
        JFWDEG      +2 IF [SB + 3] >= [SB - 0]  ; Jump if arg0 >= 2
        RETVS       [SB - 1]                    ; Return 1
        SUBI        [SB - 0] = [SB + 3] - [SB - 1]
        SUBI        [SB - 2] = [SB - 0] - [SB - 1]
        PUSHSCHK    [SB - 0]
        CALLNWO     G[2]                        ; Call fib(n - 1)
        RESULT      [SB - 0], POP 1
        PUSHCHK     [SB - 2]
        CALLNWO     G[2]                        ; Call fib(n - 2)
        RESULT      [SB - 2], POP 1
        CADDI       [SB - 2] += [SB - 0]
        RETVS       [SB - 2]
        */
        woort_OpCode_PUSHRCHK(3),
        woort_OpCode_LOAD(0, 0),
        woort_OpCode_LOAD(1, -1),
        woort_OpCode_JFWDEG(3, 0, 2),
        woort_OpCode_RETVS(-1),
        woort_OpCode_SUBI(3, -1, 0),
        woort_OpCode_SUBI(0, -1, -2),
        woort_OpCode_PUSHSCHK(0),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, 0),
        woort_OpCode_PUSHSCHK(-2),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, -2),
        woort_OpCode_CADDI(0, -2),
        woort_OpCode_RETVS(-2),

        // Function: main()=> void
/*
        PUSHRCHK    1                           ; Reserving stack.
        PUSHCCHK    G[3]                        ; Push 5
        CALLNWO     G[2]                        ; Call fib(5)
        RESULT      [SB - 0], POP 1
        PUSHSCHK    [SB - 0]
        CALLNFP     G[4]                        ; Call print_int(fib(5))
        POPR        1
        RET
*/
        woort_OpCode_PUSHRCHK(1),
        woort_OpCode_PUSHCCHK(3),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, 0),
        woort_OpCode_PUSHSCHK(0),
        woort_OpCode_CALLNFP(4),
        woort_OpCode_POPR(1),
        woort_OpCode_RET(),
    };

    woort_CodeEnv* codeenv;
    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        5,
        &codeenv);

    codeenv->m_data_begin[0].m_integer = 2;
    codeenv->m_data_begin[1].m_integer = 1;
    codeenv->m_data_begin[2].m_script_function = codeenv->m_code_begin + 0;
    codeenv->m_data_begin[3].m_integer = 10;
    codeenv->m_data_begin[4].m_native_or_jit_function = &print_int;
    // 常量和静态存储区共享相同的存储区域，换言之，在 VM 层面没有本质区别
```

如上所示的代码包含了两个函数和五个常量/静态存储单元，然后可以通过:

```c
    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin + 15);

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_destroy(vm);
```

如上的代码，调用 `codeenv->m_code_begin + 15`，也就是函数 `main()=> void`

## Function

一个函数总是以 `PUSHRCHK` 指令作为起始指令以预留所需的栈空间（除非这个函数不需要额外的栈空间），并最终以 `RET`，`RETVS` 或者 `RETVC` 返回指令为结束

函数的参数需要在 `CALL*` 系列指令执行前按逆序压入栈中，也就是栈顶 [SP+1] 处为首个参数，第二个参数在 [SP+2]，以此类推。

函数调用正式发生于 `CALLNWO` `CALLNFP` `CALLNJIT` 或 `CALL`，此指令会向栈中 PUSH 两个 value 的位置以存放返回位置信息，然后令 SB = SP, 开启一个新的栈帧。

这意味着，对于当前函数而言，返回上一层调用栈的返回地址储存在 [SB+1], 上一层调用栈的 SB 位置和调用方式等细节储存着 [SB+2]，函数的第一个参数是 [SB+3]，第二个参数是 [SB+4]，以此类推。

函数返回时，还原调用位置和 BP 位置之后，会从栈中弹出两个元素，并将返回值储存在 [SP] 处（如果有返回值），可以用 `RESULT` 命令获取这个结果。此外，调用约定要求调用方负责清理参数栈，`RESULT` 指令包含有弹栈操作的功能。如果函数返回值类型是 `void`，则可以直接使用 `POPR` 指；如果有 `UNPACKVEC` 或者 `UNPACKVECX` 这样的运行时参数包展开操作，可以用 `POPRS` 指令指示具体的弹出数量。

## IR 接口风格

* 和 LLVM 不一样，操作数不需要带有类型信息，WooRT 是 Woolang 的运行时，Woolang 的编译器前端负责执行类型检查，保证类型正确
* 提供无限虚拟寄存器 woort_IRValue*（SSA），为不同寻址返回的相同功能指令提供统一的指令生成接口，由 IR 负责决定生成的最终指令
* 以 Block 为基础，不提供跳转指令，由 IR 在块之间自然插入
* 由于 SSA 编写代码不大方便，提供 `woort_IRStorage`，Storage 允许 load 和 store，并由 IR 自动转化为对应的 PHI 指令
* 做好指令选择，例如，遇到 `ADDI c = a + c` 之类的指令时，应当使用 `CADDI`，条件跳转时，如果没有超出跳转范围，就不要生成额外的二级跳转指令，尽可能保证 `指令数量少，寻址次数少`

## 示例代码

以上述斐波那契程序为例，WooRT IR 接口使用起来应当是这样的：

```c
woort_IRCompiler* irc;
woort_IRCompiler_init(&irc);

// 为常量分配全局存储索引
woort_IRGlobalIndex const_val_2 = woort_IRCompiler_allocate_global(irc);
woort_IRGlobalIndex const_val_1 = woort_IRCompiler_allocate_global(irc);
woort_IRGlobalIndex const_val_func_jit = woort_IRCompiler_allocate_global(irc);
woort_IRGlobalIndex const_val_10 = woort_IRCompiler_allocate_global(irc);
woort_IRGlobalIndex const_val_print_int = woort_IRCompiler_allocate_global(irc);

// 创建 fib 函数
woort_IRFunction* irfunc_fib;
if (!woort_IRCompiler_add_function(irc, &irfunc_fib))
{
    // 处理错误...
}

// 获取函数的入口基本块
woort_IRBlock* const irblock_fib_entry = 
    woort_IRFunction_get_entry_block(irfunc_fib);

woort_IRBlock* irblock_less_then_2;
if (!woort_IRFunction_add_block(irfunc_fib, &irblock_less_then_2))
{
    // 处理错误...
}

woort_IRBlock* irblock_greater_then_2;
if (!woort_IRFunction_add_block(irfunc_fib, &irblock_greater_then_2))
{
    // 处理错误...
}

const woort_IRValue* const val1 = 
    woort_IRFunction_load_const(irfunc_fib, const_val_1);
const woort_IRValue* const val2 = 
    woort_IRFunction_load_const(irfunc_fib, const_val_2);
const woort_IRValue* const arg0 = 
    woort_IRFunction_load_argument(irfunc_fib, 0);

(void)woort_IRBlock_condbr_less_then(
    irblock_fib_entry, 
    arg0,
    val2,
    irblock_less_then_2,
    irblock_greater_then_2);

// Return 1;
(void)woort_IRBlock_ret(irblock_less_then_2, val1);

const woort_IRValue* const n_sub_1 = 
    woort_IRBlock_SUBI(irblock_greater_then_2, arg0, val1);

woort_IRBlock_PUSH(n_sub_1);

const woort_IRValue* fib_n_sub_1;
woort_IRBlock_CALLNWO(irblock_greater_then_2, const_val_func_jit, 1 /* ARGC */, &fib_n_sub_1);

const woort_IRValue* const n_sub_2 = 
    woort_IRBlock_SUBI(irblock_greater_then_2, n_sub_1, val1);

woort_IRBlock_PUSH(n_sub_2);

const woort_IRValue* fib_n_sub_2;
woort_IRBlock_CALLNWO(irblock_greater_then_2, const_val_func_jit, 1, &fib_n_sub_2);  

const woort_IRValue* const result =

// Return fib(n - 1) + fib(n - 2)
(void)woort_IRBlock_ret(irblock_greater_then_2, result);


/////////////////////////////////////////////////////////////////////////

// 创建 main 函数
woort_IRFunction* irfunc_main;
if (!woort_IRCompiler_add_function(irc, &irfunc_main))
{
    // 处理错误...
}

woort_IRBlock* const irblock_main_entry =
    woort_IRFunction_get_entry_block(irfunc_main);

const woort_IRValue* const const_10 =
    woort_IRFunction_load_const(irfunc_main, const_val_10);

woort_IRBlock_PUSH(const_10);

const woort_IRValue* fib_result; 
woort_IRBlock_CALLNWO(irblock_main_entry, const_val_func_jit, 1, &fib_result);

woort_IRBlock_PUSH(fib_result);

woort_IRBlock_CALLNFP(irblock_main_entry, const_val_print_int, 1, NULL);

(void)woort_IRBlock_ret_void(irblock_main_entry);

woort_CodeEnv* code_env;
if (!woort_IRCompiler_finish(irc, &code_env))
{
    // 处理错误...
}

// 填充常量，获取符号表等等。。。
```

## 注意

* IRBlock 接口中，指令部分保持大写
* woort_IRBlock_CALLNFP/woort_IRBlock_CALLNWO/woort_IRBlock_CALLNJIT/woort_IRBlock_CALL 包含参数数量和返回值接收；
    参数数量仅用于生成 POPR/RESULT 指令时，指示所需弹出清理栈上空间的数量，如果返回值接收指定为 NULL，则不使用 RESULT
    指令，直接 POPR.