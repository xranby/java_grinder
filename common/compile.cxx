/**
 *  Java Grinder
 *  Author: Michael Kohn
 *   Email: mike@mikekohn.net
 *     Web: http://www.mikekohn.net/
 * License: GPL
 *
 * Copyright 2014 by Michael Kohn
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "JavaClass.h"
#include "compile.h"
#include "invoke.h"
#include "table_java_instr.h"

// http://docs.oracle.com/javase/specs/jvms/se7/html/jvms-6.html

#define UNIMPL() printf("Opcode (%d) '%s' unimplemented\n", bytes[pc], table_java_instr[(int)bytes[pc]].name); ret = -1;

//#define CONST_STACK_SIZE 4

static uint8_t cond_table[] =
{
  COND_EQUAL,         // 159 (0x9f) if_icmpeq
  COND_NOT_EQUAL,     // 160 (0xa0) if_icmpne
  COND_LESS,          // 161 (0xa1) if_icmplt
  COND_GREATER_EQUAL, // 162 (0xa2) if_icmpge
  COND_GREATER,       // 163 (0xa3) if_icmpgt
  COND_LESS_EQUAL,    // 164 (0xa4) if_icmple
};

static void fill_label_map(uint8_t *label_map, int label_map_len, uint8_t *bytes, int code_len, int pc_start)
{
int pc = pc_start;
int wide = 0;
int address;

  memset(label_map, 0, label_map_len);

  while(pc - pc_start < code_len)
  {
    int opcode = bytes[pc];

    if (opcode == 0xc4) { wide = 1; pc++; continue; }

    switch(opcode)
    {
        case 0x99:  // ifeq
        case 0x9a:  // ifne
        case 0x9b:  // iflt
        case 0x9c:  // ifge
        case 0x9d:  // ifgt
        case 0x9e:  // ifle
        case 0x9f:  // if_icmpeq
        case 0xa0:  // if_icmpne
        case 0xa1:  // if_icmplt
        case 0xa2:  // if_icmpge
        case 0xa3:  // if_icmpgt
        case 0xa4:  // if_icmple
        case 0xa5:  // if_acmpeq
        case 0xa6:  // if_acmpne
        case 0xa7:  // goto
        case 0xa8:  // jsr
        case 0xc6:  // ifnull
        case 0xc7:  // ifnonnull
        {
          int16_t offset = GET_PC_INT16(1);
          address = (pc + offset) - pc_start;
          if (address < 0) { printf("Internal error: %s:%d\n", __FILE__, __LINE__); return; }
          label_map[address / 8] |= 1 << (address % 8);
          break;
        }
        case 0xc8:  // goto_w
        case 0xc9:  // jsr_w
        {
          int32_t offset = GET_PC_INT32(1);
          address = (pc + offset) - pc_start;
          if (address < 0) { printf("Internal error: %s:%d\n", __FILE__, __LINE__); return; }
          label_map[address / 8] |= 1 << (address % 8);
          break;
        }
      default:
        break;
    }

    if (wide == 1)
    {
      pc += table_java_instr[opcode].wide;
      wide = 0;
    }
      else
    {
      pc += table_java_instr[opcode].normal;
    }
  }
}

// FIXME - Too many parameters :(.
static int optimize_const(JavaClass *java_class, Generator *generator, char *method_name, uint8_t *bytes, int pc, int pc_end, int address, int const_val)
{
int const_vals[2];

  // istore_x
  if (bytes[pc] >= 0x3b && bytes[pc] <= 0x3e) // istore_x
  {
    if (generator->set_integer_local(bytes[pc] - 0x3b, const_val) != 0)
    { return 0; }
    return 1;
  }

  // istore
  if (pc + 1 < pc_end && bytes[pc] == 0x36)
  {
    if (generator->set_integer_local(bytes[pc+1], const_val) != 0)
    { return 0; }
    return 2;
  }

  // istore wide
  if (pc + 2 < pc_end && bytes[pc] == 0xc4 && bytes[pc+1] == 0x36)
  {
    if (generator->set_integer_local(GET_PC_UINT16(1), const_val) != 0)
    { return 0; }
    return 3;
  }

  // 159 (0x9f) if_icmpeq
  // 160 (0xa0) if_icmpne
  // 161 (0xa1) if_icmplt
  // 162 (0xa2) if_icmpge
  // 163 (0xa3) if_icmpgt
  // 164 (0xa4) if_icmple
  if (pc + 2 < pc_end && bytes[pc] >= 0x9f && bytes[pc] <= 0xa4)
  {
    char label[128];
    sprintf(label, "%s_%d", method_name, address + GET_PC_INT16(1));
    if (generator->jump_cond_integer(label, cond_table[bytes[pc]-159], const_val) == -1)
    { return 0; }
    return 3;
  }

  // invokestatic with one const
  if (pc + 2 < pc_end && bytes[pc] == 0xb8)
  {
    int ref = GET_PC_UINT16(1);
    if (invoke_static(java_class, ref, generator, &const_val, 1) != 0)
    { return 0; }
    return 3;
  }

  // invokestatic with two const
  // 02 (0x02) iconst_m1
  // 03 (0x03) iconst_0
  // 04 (0x04) iconst_1
  // 05 (0x05) iconst_2
  // 06 (0x06) iconst_3
  // 07 (0x07) iconst_4
  // 08 (0x08) iconst_5
  if (pc + 3 < pc_end &&
      bytes[pc] >= 0x02 && bytes[pc] <= 0x08 &&
      bytes[pc+1] == 0xb8)
  {
    const_vals[0] = const_val;
    const_vals[1] = (int8_t)bytes[pc] - 3;
    int ref = GET_PC_UINT16(2);
    if (invoke_static(java_class, ref, generator, const_vals, 2) != 0)
    { return 0; }
    return 4;
  }

  // FIXME - add more invoke(const,const) combinations.

  return 0;
}

int compile_method(JavaClass *java_class, int method_id, Generator *generator)
{
struct methods_t *method = java_class->get_method(method_id);
uint8_t *bytes = method->attributes[0].info;
int pc;
const float fzero = 0.0;
const float fone = 1.0;
const float ftwo = 2.0;
int wide = 0;
int pc_start;
int max_stack;
int max_locals;
int code_len;
uint32_t ref;
struct generic_32bit_t *gen32;
struct constant_float_t *constant_float;
uint8_t *label_map;
int ret = 0;
char label[128];
char method_name[64];
uint16_t *operand_stack;
uint16_t operand_stack_ptr = 0;
//uint32_t const_stack[CONST_STACK_SIZE];
//int const_stack_ptr = 0;
int const_val;

  if (java_class->get_method_name(method_name, sizeof(method_name), method_id) != 0)
  {
    strcpy(method_name, "error");
  }

  printf("--- Compiling method '%s' method_id=%d\n", method_name, method_id);

  if (strcmp(method_name, "<init>") == 0 || method_name[0] == 0)
  {
    printf("Skipping method <--\n");
    return 0;
  }

  if (strcmp(method_name, "main") != 0)
  {
    char method_sig[64];
    java_class->get_name_constant(method_sig, sizeof(method_sig), method->descriptor_index);

    char *s = method_sig + 1;
    while(*s != ')' && *s != 0) { s++; }
    *s = 0;
    method_sig[0] = '_';
    if (method_sig[1] != 0 ) { strcat(method_name, method_sig); }
    printf("Using method name '%s'\n", method_name);
  }

  // bytes points to the method attributes info for the method.
  max_stack = ((int)bytes[0]<<8) | ((int)bytes[1]);
  max_locals = ((int)bytes[2]<<8) | ((int)bytes[3]);
  code_len = ((int)bytes[4]<<24) |
             ((int)bytes[5]<<16) |
             ((int)bytes[6]<<8) |
             ((int)bytes[7]);
  pc_start = (((int)bytes[code_len+8]<<8) |
             ((int)bytes[code_len+9])) + 8;
  pc = pc_start;

  generator->method_start(max_locals, method_name);
  operand_stack = (uint16_t *)alloca(max_stack * sizeof(uint16_t));

  int label_map_len = (code_len / 8) + 1;
  label_map = (uint8_t *)alloca(label_map_len);
  fill_label_map(label_map, label_map_len, bytes, code_len, pc_start);

#ifdef DEBUG
printf("pc=%d\n", pc);
printf("max_stack=%d\n", max_stack);
printf("max_locals=%d\n", max_locals);
printf("code_len=%d\n", code_len);
#endif

  while(pc - pc_start < code_len)
  {
    int address = pc - pc_start;
#ifdef DEBUG
    printf("pc=%d opcode=%d (0x%02x)\n", address, bytes[pc], bytes[pc]);
#endif
    if ((label_map[address / 8] & (1 << (address % 8))) != 0)
    {
      sprintf(label, "%s_%d", method_name, address);
      generator->label(label);
    }

    switch(bytes[pc])
    {
      case 0: // nop (0x00)
        pc++;
        break;

      case 1: // aconst_null (0x01)
        UNIMPL()
        pc++;
        break;

      case 2: // iconst_m1 (0x02)
      case 3: // iconst_0 (0x03)
      case 4: // iconst_1 (0x04)
      case 5: // iconst_2 (0x05)
      case 6: // iconst_3 (0x06)
      case 7: // iconst_4 (0x07)
      case 8: // iconst_5 (0x08)
        const_val = uint8_t(bytes[pc])-3;
        ret = optimize_const(java_class, generator, method_name, bytes, pc + 1, pc_start + code_len, address + 1, const_val);
        if (ret == 0)
        {
          ret = generator->push_integer(const_val);
        }
          else
        {
          pc += ret;
          ret = 0;
        }
        pc++;
        break;

      case 9:  // lconst_0 (0x09)
      case 10: // lconst_1 (0x0a)
        ret = generator->push_long(bytes[pc]-9);
        pc++;
        break;

      case 11: // fconst_0 (0x0b)
        ret = generator->push_float(fzero);
        pc++;
        break;

      case 12: // fconst_1 (0x0c)
        ret = generator->push_float(fone);
        pc++;
        break;

      case 13: // fconst_2 (0x0d)
        ret = generator->push_float(ftwo);
        pc++;
        break;

      case 14: // dconst_0 (0x0e)
        ret = generator->push_double(fzero);
        pc++;
        break;

      case 15: // dconst_1 (0x0f)
        ret = generator->push_double(fone);
        pc++;
        break;

      case 16: // bipush (0x10)
        //PUSH_BYTE((char)bytes[pc+1])
        const_val = (int8_t)bytes[pc+1];
        ret = optimize_const(java_class, generator, method_name, bytes, pc + 2, pc_start + code_len, address + 2, const_val);
        if (ret == 0)
        {
          // FIXME - I don't think push_byte() is really needed.
          // push_integer() is probably more correct.
          ret = generator->push_byte(const_val);
        }
          else
        {
          pc += ret;
          ret = 0;
        }
        pc += 2;
        break;

      case 17: // sipush (0x11)
        const_val = (int16_t)((bytes[pc+1]<<8)|(bytes[pc+2]));
        ret = optimize_const(java_class, generator, method_name, bytes, pc + 3, pc_start + code_len, address + 3, const_val);
        if (ret == 0)
        {
          // FIXME - I don't think push_short() is really needed.
          // push_integer() is probably more correct.
          ret = generator->push_short(const_val);
        }
          else
        {
          pc += ret;
          ret = 0;
        }
        pc+=3;
        break;

      case 18: // ldc (0x12)
        gen32 = (generic_32bit_t *)java_class->get_constant(bytes[pc+1]);

        if (gen32->tag == CONSTANT_INTEGER)
        {
          //PUSH_INTEGER(gen32->value);
          const_val = gen32->value;
          ret = optimize_const(java_class, generator, method_name, bytes, pc + 2, pc_start + code_len, address + 2, const_val);
          if (ret == 0)
          {
            ret = generator->push_integer(const_val);
          }
            else
          {
            pc += ret;
            ret = 0;
          }
        }
          else
        if (gen32->tag == CONSTANT_FLOAT)
        {
          constant_float = (constant_float_t *)gen32;
          //PUSH_FLOAT(constant_float->value);
          ret = generator->push_float(constant_float->value);
        }
          else
        if (gen32->tag == CONSTANT_STRING)
        {
          printf("Can't do a string yet.. :(\n");
          ret = -1;
        }
          else
        {
          printf("Cannot ldc this type %d=>'%s' pc=%d\n", gen32->tag, JavaClass::tag_as_string(gen32->tag), pc);
          ret = -1;
        }

        pc += 2;
        break;

      case 19: // ldc_w (0x13)
        UNIMPL()
        pc += 3;
        break;

      case 20: // ldc2_w (0x14)
        UNIMPL()
        pc += 3;
        break;

      case 21: // iload (0x15)
        if (wide == 1)
        {
          //PUSH_INTEGER(local_vars[GET_PC_UINT16(1)]);
          ret = generator->push_integer_local(GET_PC_UINT16(1));
          pc += 3;
        }
          else
        {
          //PUSH_INTEGER(local_vars[bytes[pc+1]]);
          ret = generator->push_integer_local(bytes[pc+1]);
          pc += 2;
        }
        break;

      case 22: // lload (0x16)
        UNIMPL()
        if (wide == 1)
        {
          //PUSH_LONG(*((long long *)(local_vars+GET_PC_UINT16(1))));
          pc += 3;
        }
          else
        {
          //PUSH_LONG(*((long long *)(local_vars+bytes[pc+1])));
          pc += 2;
        }
        break;

      case 23: // fload (0x17)
        UNIMPL()
        if (wide == 1)
        {
          //PUSH_FLOAT_I(local_vars[GET_PC_UINT16(1)]);
          pc += 3;
        }
          else
        {
          //PUSH_FLOAT_I(local_vars[bytes[pc+1]]);
          pc += 2;
        }
        break;

      case 24: // dload (0x18)
        UNIMPL()
        if (wide == 1)
        {
          //PUSH_LONG(*((long long *)(local_vars+GET_PC_UINT16(1))));
          pc += 3;
        }
          else
        {
          //PUSH_LONG(*((long long *)(local_vars+bytes[pc+1])));
          pc += 2;
        }
        break;

      case 25: // aload (0x19)
        UNIMPL()
        if (wide == 1)
        {
          pc += 3;
        }
          else
        {
          pc += 2;
        }
        break;

      case 26: // iload_0 (0x1a)
      case 27: // iload_1 (0x1b)
      case 28: // iload_2 (0x1c)
      case 29: // iload_3 (0x1d)
        // Push a local integer variable on the stack
        ret = generator->push_integer_local(bytes[pc]-26);
        pc++;
        break;

      case 30: // lload_0 (0x1e)
      case 31: // lload_1 (0x1f)
      case 32: // lload_2 (0x20)
      case 33: // lload_3 (0x21)
        // push a local long variable on the stack
        //ret = generator->push_long_local(bytes[pc]-30);
        UNIMPL()
        pc++;
        break;

      case 34: // fload_0 (0x22)
        UNIMPL()
        //PUSH_FLOAT_I(local_vars[0]);
        pc++;
        break;

      case 35: // fload_1 (0x23)
        UNIMPL()
        //PUSH_FLOAT_I(local_vars[1]);
        pc++;
        break;

      case 36: // fload_2 (0x24)
        UNIMPL()
        //PUSH_FLOAT_I(local_vars[2]);
        pc++;
        break;

      case 37: // fload_3 (0x25)
        UNIMPL()
        //PUSH_FLOAT_I(local_vars[3]);
        pc++;
        break;

      case 38: // dload_0 (0x26)
        UNIMPL()
        pc++;
        break;

      case 39: // dload_1 (0x27)
        UNIMPL()
        pc++;
        break;

      case 40: // dload_2 (0x28)
        UNIMPL()
        pc++;
        break;

      case 41: // dload_3 (0x29)
        UNIMPL()
        pc++;
        break;

      case 42: // aload_0 (0x2a)
        UNIMPL()
        pc++;
        break;

      case 43: // aload_1 (0x2b)
        UNIMPL()
        pc++;
        break;

      case 44: // aload_2 (0x2c)
        UNIMPL()
        pc++;
        break;

      case 45: // aload_3 (0x2d)
        UNIMPL()
        pc++;
        break;

      case 46: // iaload (0x2e)
        UNIMPL()
        pc++;
        break;

      case 47: // laload (0x2f)
        UNIMPL()
        pc++;
        break;

      case 48: // faload (0x30)
        UNIMPL()
        pc++;
        break;

      case 49: // daload (0x31)
        UNIMPL()
        pc++;
        break;

      case 50: // aaload (0x32)
        UNIMPL()
        pc++;
        break;

      case 51: // baload (0x33)
        UNIMPL()
        pc++;
        break;

      case 52: // caload (0x34)
        UNIMPL()
        pc++;
        break;

      case 53: // saload (0x35)
        UNIMPL()
        pc++;
        break;

      case 54: // istore (0x36)
        if (wide == 1)
        {
          ret = generator->pop_integer_local(GET_PC_UINT16(1));
          //local_vars[GET_PC_UINT16(1)]=POP_INTEGER();
          pc += 3;
        }
          else
        {
          //local_vars[bytes[pc+1]]=POP_INTEGER();
          ret = generator->pop_integer_local(bytes[pc+1]);
          pc += 2;
        }
        break;

      case 55: // lstore (0x37)
        UNIMPL()
        if (wide == 1)
        {
          pc += 3;
        }
          else
        {
          pc += 2;
        }
        break;

      case 56: // fstore (0x38)
        UNIMPL()
        if (wide == 1)
        {
          pc += 3;
        }
          else
        {
          pc += 2;
        }
        break;

      case 57: // dstore (0x39)
        UNIMPL()
        if (wide == 1)
        {
          pc += 3;
        }
          else
        {
          pc += 2;
        }
        break;

      case 58: // astore (0x3a)
        UNIMPL()
        if (wide == 1)
        {
          pc += 3;
        }
          else
        {
          pc += 2;
        }
        break;

      case 59: // istore_0 (0x3b)
      case 60: // istore_1 (0x3c)
      case 61: // istore_2 (0x3d)
      case 62: // istore_3 (0x3e)
        // Pop integer off stack and store in local variable
        ret = generator->pop_integer_local(bytes[pc]-59);
        pc++;
        break;

      case 63: // lstore_0 (0x3f)
      case 64: // lstore_1 (0x40)
      case 65: // lstore_2 (0x41)
      case 66: // lstore_3 (0x42)
        // Pop long off stack and store in local variable
        //ret = generator->pop_long_local(bytes[pc]-63);
        UNIMPL()
        pc++;
        break;

      case 67: // fstore_0 (0x43)
        UNIMPL()
        //local_vars[0] = POP_FLOAT_I()
        pc++;
        break;

      case 68: // fstore_1 (0x44)
        UNIMPL()
        //local_vars[1] = POP_FLOAT_I()
        pc++;
        break;

      case 69: // fstore_2 (0x45)
        UNIMPL()
        //local_vars[2] = POP_FLOAT_I()
        pc++;
        break;

      case 70: // fstore_3 (0x46)
        UNIMPL()
        //local_vars[3] = POP_FLOAT_I()
        pc++;
        break;

      case 71: // dstore_0 (0x47)
        UNIMPL()
        pc++;
        break;

      case 72: // dstore_1 (0x48)
        UNIMPL()
        pc++;
        break;

      case 73: // dstore_2 (0x49)
        UNIMPL()
        pc++;
        break;

      case 74: // dstore_3 (0x4a)
        UNIMPL()
        pc++;
        break;

      case 75: // astore_0 (0x4b)
        UNIMPL()
        pc++;
        break;

      case 76: // astore_1 (0x4c)
        UNIMPL()
        pc++;
        break;

      case 77: // astore_2 (0x4d)
        UNIMPL()
        pc++;
        break;

      case 78: // astore_3 (0x4e)
        UNIMPL()
        pc++;
        break;

      case 79: // iastore (0x4f)
        UNIMPL()
        pc++;
        break;

      case 80: // lastore (0x50)
        UNIMPL()
        pc++;
        break;

      case 81: // fastore (0x51)
        UNIMPL()
        pc++;
        break;

      case 82: // dastore (0x52)
        UNIMPL()
        pc++;
        break;

      case 83: // aastore (0x53)
        UNIMPL()
        pc++;
        break;

      case 84: // bastore (0x54)
        UNIMPL()
        pc++;
        break;

      case 85: // castore (0x55)
        UNIMPL()
        pc++;
        break;

      case 86: // sastore (0x56)
        UNIMPL()
        pc++;
        break;

      case 87: // pop (0x57)
        // Pop off stack and discard
        ret = generator->pop();
        pc++;
        break;

      case 88: // pop2 (0x58)
        // Pop 2 things off stack and discard
        ret = generator->pop();
        ret = generator->pop();
        pc++;
        break;

      case 89: // dup (0x59)
        // Take top value on stack, and push it again
        ret = generator->dup();
        pc++;
        break;

      case 90: // dup_x1 (0x5a)
        UNIMPL()
        pc++;
        break;

      case 91: // dup_x2 (0x5b)
        UNIMPL()
        pc++;
        break;

      case 92: // dup2 (0x5c)
        // Take the top 2 values on the stack and push them again
        // value1,value2 becomes: value1,value2,value1,value2
        ret = generator->dup2();
        pc++;
        break;

      case 93: // dup2_x1 (0x5d)
        UNIMPL()
        pc++;
        break;

      case 94: // dup2_x2 (0x5e)
        UNIMPL()
        pc++;
        break;

      case 95: // swap (0x5f)
        // Take the top two values on the stack and switch them
        ret = generator->swap();
        pc++;
        break;

      case 96: // iadd (0x60)
        // Pop top two integers from stack, add them, push result
        ret = generator->add_integers();
        pc++;
        break;

      case 97: // ladd (0x61)
        // Pop top two longs from stack, add them, push result
        // ret = generator->add_longs();
        UNIMPL()
        pc++;
        break;

      case 98: // fadd (0x62)
        // Pop top two floats from stack, add them, push result
        // ret = generator->add_floats();
        UNIMPL()
        pc++;
        break;

      case 99: // dadd (0x63)
        // Pop top two doubles from stack, add them, push result
        // ret = generator->add_doubles();
        UNIMPL()
        pc++;
        break;

      case 100: // isub (0x64)
        // Pop top two integers from stack, subtract them, push result
        // *(stack-1) - *(stack-0)
        ret = generator->sub_integers();
        pc++;
        break;

      case 101: // lsub (0x65)
        // Pop top two longs from stack, subtract them, push result
        // *(stack-1) - *(stack-0)
        // ret = generator->sub_longs();
        UNIMPL()
        pc++;
        break;

      case 102: // fsub (0x66)
        // Pop top two floats from stack, subtract them, push result
        // *(stack-1) - *(stack-0)
        // ret = generator->sub_floats();
        UNIMPL()
        pc++;
        break;

      case 103: // dsub (0x67)
        // Pop top two doubles from stack, subtract them, push result
        // *(stack-1) - *(stack-0)
        // ret = generator->sub_doubles();
        UNIMPL()
        pc++;
        break;

      case 104: // imul (0x68)
        // Pop top two integers from stack, multiply them, push result
        ret = generator->mul_integers();
        pc++;
        break;

      case 105: // lmul (0x69)
        // Pop top two longs from stack, multiply them, push result
        // ret = generator->mul_longs();
        UNIMPL()
        pc++;
        break;

      case 106: // fmul (0x6a)
        // Pop top two floats from stack, multiply them, push result
        // ret = generator->mul_floats();
        UNIMPL()
        pc++;
        break;

      case 107: // dmul (0x6b)
        // Pop top two doubles from stack, multiply them, push result
        // ret = generator->mul_doubles();
        UNIMPL()
        pc++;
        break;

      case 108: // idiv (0x6c)
        // Pop top two integers from stack, divide them, push result
        ret = generator->div_integers();
        pc++;
        break;

      case 109: // ldiv (0x6d)
        UNIMPL()
        pc++;
        break;

      case 110: // fdiv (0x6e)
        UNIMPL()
        pc++;
        break;

      case 111: // ddiv (0x6f)
        UNIMPL()
        pc++;
        break;

      case 112: // irem (0x70)
        // Pop top two integers from stack, divide them, push result
        ret = generator->mod_integers();
        pc++;
        break;

      case 113: // lrem (0x71)
        UNIMPL()
        pc++;
        break;

      case 114: // frem (0x72)
        UNIMPL()
        pc++;
        break;

      case 115: // drem (0x73)
        UNIMPL()
        pc++;
        break;

      case 116: // ineg (0x74)
        // negate the top integer on the stack
        ret = generator->neg_integer();
        pc++;
        break;

      case 117: // lneg (0x75)
        // negate the top long on the stack
        UNIMPL()
        pc++;
        break;

      case 118: // fneg (0x76)
        UNIMPL()
        pc++;
        break;

      case 119: // dneg (0x77)
        UNIMPL()
        pc++;
        break;

      case 120: // ishl (0x78)
        // Pop two integer values from stack shift left and push result
        // *(stack-1) << *(stack-0)
        ret = generator->shift_left_integer();
        pc++;
        break;

      case 121: // lshl (0x79)
        // Pop two long values from stack shift left and push result
        // *(stack-1) << *(stack-0)
        UNIMPL()
        pc++;
        break;

      case 122: // ishr (0x7a)
        // Pop two integer values from stack shift right and push result
        // *(stack-1) >> *(stack-0)
        ret = generator->shift_right_integer();
        pc++;
        break;

      case 123: // lshr (0x7b)
        // Pop two long values from stack shift right and push result
        // *(stack-1) >> *(stack-0)
        UNIMPL()
        pc++;
        break;

      case 124: // iushr (0x7c)
        // Pop two unsigned integer values from stack shift left and push result
        // *(stack-1) <<< *(stack-0)
        ret = generator->shift_right_uinteger();
        pc++;
        break;

      case 125: // lushr (0x7d)
        // Pop two unsigned long values from stack shift left and push result
        // *(stack-1) <<< *(stack-0)
        UNIMPL()
        pc++;
        break;

      case 126: // iand (0x7e)
        // Pop top two integers from stack, and them, push result
        ret = generator->and_integer();
        pc++;
        break;

      case 127: // land (0x7f)
        // Pop top two longs from stack, and them, push result
        UNIMPL()
        pc++;
        break;

      case 128: // ior (0x80)
        // Pop top two integers from stack, or them, push result
        ret = generator->or_integer();
        pc++;
        break;

      case 129: // lor (0x81)
        // Pop top two longs from stack, or them, push result
        UNIMPL()
        pc++;
        break;

      case 130: // ixor (0x82)
        // Pop top two ints from stack, xor them, push result
        ret = generator->xor_integer();
        pc++;
        break;

      case 131: // lxor (0x83)
        // Pop top two longs from stack, xor them, push result
        UNIMPL()
        pc++;
        break;

      case 132: // iinc (0x84)
        if (wide == 1)
        {
          //local_vars[GET_PC_UINT16(1)] += GET_PC_INT16(3);
          ret = generator->inc_integer(GET_PC_UINT16(1), GET_PC_INT16(3));
          pc += 5;
        }
          else
        {
          //local_vars[bytes[pc+1]] += ((char)bytes[pc+2]);
          ret = generator->inc_integer(bytes[pc+1], bytes[pc+2]);
          pc += 3;
        }
        break;

      case 133: // i2l (0x85)
        // Pop top integer from stack and push as a long
        UNIMPL()
        pc++;
        break;

      case 134: // i2f (0x86)
        // Pop top integer from stack and push as a float
        UNIMPL()
        pc++;
        break;

      case 135: // i2d (0x87)
        // Pop top integer from stack and push as a double
        UNIMPL()
        pc++;
        break;

      case 136: // l2i (0x88)
        // Pop top long from stack and push as a integer
        UNIMPL()
        pc++;
        break;

      case 137: // l2f (0x89)
        // Pop top long from stack and push as a float
        UNIMPL()
        pc++;
        break;

      case 138: // l2d (0x8a)
        // Pop top long from stack and push as a double
        UNIMPL()
        pc++;
        break;

      case 139: // f2i (0x8b)
        // Pop top float from stack and push as a integer
        UNIMPL()
        pc++;
        break;

      case 140: // f2l (0x8c)
        // Pop top float from stack and push as a long
        UNIMPL()
        pc++;
        break;

      case 141: // f2d (0x8d)
        // Pop top float from stack and push as a double
        UNIMPL()
        pc++;
        break;

      case 142: // d2i (0x8e)
        // Pop top double from stack and push as a integer
        UNIMPL()
        pc++;
        break;

      case 143: // d2l (0x8f)
        // Pop top double from stack and push as a long
        UNIMPL()
        pc++;
        break;

      case 144: // d2f (0x90)
        // Pop top double from stack and push as a float
        UNIMPL()
        pc++;
        break;

      case 145: // i2b (0x91)
        // Pop top integer from stack and push as a byte
        UNIMPL()
        pc++;
        break;

      case 146: // i2c (0x92)
        // Pop top integer from stack and push as a char
        UNIMPL()
        pc++;
        break;

      case 147: // i2s (0x93)
        // Pop top integer from stack and push as a short
        UNIMPL()
        pc++;
        break;

      case 148: // lcmp (0x94)
        UNIMPL()
        pc++;
        break;

      case 149: // fcmpl (0x95)
        UNIMPL()
        pc++;
        break;

      case 150: // fcmpg (0x96)
        UNIMPL()
        pc++;
        break;

      case 151: // dcmpl (0x97)
        UNIMPL()
        pc++;
        break;

      case 152: // dcmpg (0x98)
        UNIMPL()
        pc++;
        break;

      case 153: // ifeq (0x99)
      case 154: // ifne (0x9a)
      case 155: // iflt (0x9b)
      case 156: // ifge (0x9c)
      case 157: // ifgt (0x9d)
      case 158: // ifle (0x9e)
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT16(1));
        ret = generator->jump_cond(label, cond_table[bytes[pc]-153]);
        pc += 3;
        //value1 = POP_INTEGER();
        //if (value1 == 0)
        //{ pc += GET_PC_INT16(1); }
        //  else
        //{ pc += 3; }
        break;

      case 159: // if_icmpeq (0x9f)
      case 160: // if_icmpne (0xa0)
      case 161: // if_icmplt (0xa1)
      case 162: // if_icmpge (0xa2)
      case 163: // if_icmpgt (0xa3)
      case 164: // if_icmple (0xa4)
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT16(1));
        ret = generator->jump_cond_integer(label, cond_table[bytes[pc]-159]);
        pc += 3;

        //value1 = POP_INTEGER();
        //value2 = POP_INTEGER();
        //if (value2 <= value1)
        //{ pc += GET_PC_INT16(1); }
        //  else
        //{ pc += 3; }
        break;

      case 165: // if_acmpeq (0xa5)
        UNIMPL()
        break;

      case 166: // if_acmpne (0xa6)
        UNIMPL()
        break;

      case 167: // goto (0xa7)
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT16(1));
        ret = generator->jump(label);
        pc += 3;
        //pc += GET_PC_INT16(1);
        break;

      case 168: // jsr (0xa8)
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT16(1));
        ret = generator->call(label);
        pc += 3;
        //PUSH_INTEGER(pc+3);
        //pc += GET_PC_INT16(1);
        break;

      case 169: // ret (0xa9)
        // FIXME - "Continue execution from address taken from a local
        // variable #index (the asymmetry with jsr is intentional).
        // The hell does that mean?  jsr pushes the return address on the
        // stack.. this thing shouldn't have to have an index.
#if 0
        if (wide == 1)
        {
          //pc = local_vars[GET_PC_UINT16(1)];
          ret = generator->return_local(GET_PC_UINT16(1), max_locals);
          pc += 3;
        }
          else
        {
          //pc = local_vars[bytes[pc+1]];
          ret = generator->return_local(bytes[pc+1], max_locals);
          pc += 2;
        }
#endif
        ret = -1;
        break;

      case 170: // tableswitch (0xaa)
        UNIMPL()
        pc++;
        break;

      case 171: // lookupswitch (0xab)
        UNIMPL()
        pc++;
        break;

      case 172: // ireturn (0xac)
        //value1 = POP_INTEGER()
        //stack_values = java_stack->values;
        //stack_types = java_stack->types;
        //PUSH_INTEGER(value1);
        ret = generator->return_integer(max_locals);
        pc++;
        break;

      case 173: // lreturn (0xad)
        UNIMPL()
        pc++;
        //lvalue1 = POP_LONG()
        //stack_values = java_stack->values;
        //stack_types = java_stack->types;
        //PUSH_LONG(lvalue1);
        break;

      case 174: // freturn (0xae)
        UNIMPL()
        pc++;
        //value1 = POP_INTEGER()
        //stack_values = java_stack->values;
        //stack_types = java_stack->types;
        //PUSH_INTEGER(value1);
        break;

      case 175: // dreturn (0xaf)
        UNIMPL()
        pc++;
        //dvalue1 = POP_DOUBLE()
        //stack_values = java_stack->values;
        //stack_types = java_stack->types;
        //PUSH_LONG(dvalue1);
        break;

      case 176: // areturn (0xb0)
        UNIMPL()
        pc++;
        break;

      case 177: // return (0xb1)
        ret = generator->return_void(max_locals);
        pc++;
        break;

      case 178: // getstatic (0xb2)
        ref = GET_PC_UINT16(1);
        operand_stack[operand_stack_ptr++] = ref;
        pc+=3;
#ifdef DEBUG
#if 0
        {
          char class_name[128];
          char name[128];
          char type[128];
          java_class->get_ref_name_type(name, type, sizeof(name), ref);
          java_class->get_class_name(class_name, sizeof(class_name), ref);
          printf("getstatic '%s as %s' from %s\n", name, type, class_name);
        }
#endif
#endif
        // FIXME - need to test for private/protected and that it's a field
        // printf("getstatic %d\n",GET_PC_UINT16(1));
        //PUSH_REF(GET_PC_UINT16(1));
        break;
      case 179: // putstatic (0xb3)
        UNIMPL()
        pc+=3;
        break;

      case 180: // getfield (0xb4)
        UNIMPL()
        pc+=3;
        break;

      case 181: // putfield (0xb5)
        UNIMPL()
        pc+=3;
        break;

      case 182: // invokevirtual (0xb6)
        ref = GET_PC_UINT16(1);
        if (operand_stack_ptr == 0)
        {
          printf("Error: empty operand_stack\n");
          ret = -1;
          break;
        }
        
        ret = invoke_virtual(java_class, ref, operand_stack[--operand_stack_ptr], generator);
        pc += 3;
        break;

      case 183: // invokespecial (0xb7)
        UNIMPL()
        break;

      case 184: // invokestatic (0xb8)
        ref = GET_PC_UINT16(1);
        ret = invoke_static(java_class, ref, generator);
        pc += 3;
        break;

      case 185: // invokeinterface (0xb9)
        UNIMPL()
        break;

      case 186: // invokedynamic (0xba)
        UNIMPL()
        break;

      case 187: // new (0xbb)
        UNIMPL()
        break;

      case 188: // newarray (0xbc)
        UNIMPL()
        break;

      case 189: // anewarray (0xbd)
        UNIMPL()
        break;

      case 190: // arraylength (0xbe)
        UNIMPL()
        break;

      case 191: // athrow (0xbf)
        UNIMPL()
        break;

      case 192: // checkcast (0xc0)
        UNIMPL()
        break;

      case 193: // instanceof (0xc1)
        UNIMPL()
        break;

      case 194: // monitorenter (0xc2)
        UNIMPL()
        break;

      case 195: // monitorexit (0xc3)
        UNIMPL()
        break;

      case 196: // wide (0xc4)
        wide=1;
        pc++;
        continue;

      case 197: // multianewarray (0xc5)
        UNIMPL()
        break;

      case 198: // ifnull (0xc6)
        UNIMPL()
        break;

      case 199: // ifnonnull (0xc7)
        UNIMPL()
        break;

      case 200: // goto_w (0xc8)
        //pc += (short)((((unsigned int)bytes[pc+1])<<24) |
        //              (((unsigned int)bytes[pc+2])<<16) |
        //              (((unsigned int)bytes[pc+3])<<8) |
        //              bytes[pc+4]);
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT32(1));
        ret = generator->jump(label);
        pc += 5;
        break;

      case 201: // jsr_w (0xc9)
        //PUSH_INTEGER(pc+5);
        //pc += GET_PC_INT32(1);
        sprintf(label, "%s_%d", method_name, address + GET_PC_INT32(1));
        ret = generator->call(label);
        pc += 5;
        break;

      case 202: // breakpoint (0xca)
        ret = generator->brk();
        pc++;
        break;

      case 203: // not_valid (0xfe)
      case 204: // not_valid (0xfe)
      case 205: // not_valid (0xfe)
      case 206: // not_valid (0xfe)
      case 207: // not_valid (0xfe)
      case 208: // not_valid (0xfe)
      case 209: // not_valid (0xfe)
      case 210: // not_valid (0xfe)
      case 211: // not_valid (0xfe)
      case 212: // not_valid (0xfe)
      case 213: // not_valid (0xfe)
      case 214: // not_valid (0xfe)
      case 215: // not_valid (0xfe)
      case 216: // not_valid (0xfe)
      case 217: // not_valid (0xfe)
      case 218: // not_valid (0xfe)
      case 219: // not_valid (0xfe)
      case 220: // not_valid (0xfe)
      case 221: // not_valid (0xfe)
      case 222: // not_valid (0xfe)
      case 223: // not_valid (0xfe)
      case 224: // not_valid (0xfe)
      case 225: // not_valid (0xfe)
      case 226: // not_valid (0xfe)
      case 227: // not_valid (0xfe)
      case 228: // not_valid (0xfe)
      case 229: // not_valid (0xfe)
      case 230: // not_valid (0xfe)
      case 231: // not_valid (0xfe)
      case 232: // not_valid (0xfe)
      case 233: // not_valid (0xfe)
      case 234: // not_valid (0xfe)
      case 235: // not_valid (0xfe)
      case 236: // not_valid (0xfe)
      case 237: // not_valid (0xfe)
      case 238: // not_valid (0xfe)
      case 239: // not_valid (0xfe)
      case 240: // not_valid (0xfe)
      case 241: // not_valid (0xfe)
      case 242: // not_valid (0xfe)
      case 243: // not_valid (0xfe)
      case 244: // not_valid (0xfe)
      case 245: // not_valid (0xfe)
      case 246: // not_valid (0xfe)
      case 247: // not_valid (0xfe)
      case 248: // not_valid (0xfe)
      case 249: // not_valid (0xfe)
      case 250: // not_valid (0xfe)
      case 251: // not_valid (0xfe)
      case 252: // not_valid (0xfe)
      case 253: // not_valid (0xfe)
        UNIMPL()
        pc++;
        break;

      case 254: // impdep1 (0xfe)
        UNIMPL()
        break;

      case 255: // impdep2 (0xff)
        UNIMPL()
        break;
    }

    if (ret != 0) { break; }

#ifdef DEBUG
    //stack_dump(stack_values_start, stack_types, stack_ptr);
#endif

    //printf("pc=%d opcode=%d (0x%02x)\n", pc - pc_start, bytes[pc], bytes[pc]);
    //if (pc - pc_start >= code_len) { break; }

    wide = 0;
  }

  generator->method_end(max_locals);

  return ret;
}



