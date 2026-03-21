#pragma once

/*
 * woort_ir.h
 * 
 * WooRT IR 公共入口头文件
 * 
 * 这是一个类似 AsmJit 风格的低级 IR 接口，
 * 提供无限虚拟寄存器和 SSA 形式。
 */

#include "woort_ir_types.h"
#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_ir_block.h"
